#include "HttpServer.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "Ws2_32.lib")
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

#include <cctype>
#include <sstream>
#include <vector>

namespace {

// The few socket calls that differ between Winsock and BSD sockets.
#ifdef _WIN32
using SocketHandle = SOCKET;

bool startSockets()
{
    WSADATA wsaData;
    return WSAStartup(MAKEWORD(2, 2), &wsaData) == 0;
}

void stopSockets()
{
    WSACleanup();
}

void closeSocket(SocketHandle sock)
{
    closesocket(sock);
}

// Closing the listen socket is what unblocks a thread waiting in accept().
void closeListenSocket(SocketHandle sock)
{
    closesocket(sock);
}

void setReceiveTimeout(SocketHandle sock, int timeoutMs)
{
    DWORD timeout = timeoutMs;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
}

int sendString(SocketHandle sock, const std::string& data)
{
    return send(sock, data.c_str(), static_cast<int>(data.length()), 0);
}

void nameThread(std::thread& thread)
{
    SetThreadDescription(thread.native_handle(), L"HttpServerLoop");
}
#else
using SocketHandle = int;
constexpr SocketHandle INVALID_SOCKET = -1;
constexpr int SOCKET_ERROR = -1;

bool startSockets()
{
    return true;
}

void stopSockets()
{
}

void closeSocket(SocketHandle sock)
{
    close(sock);
}

// close() alone does not wake a thread blocked in accept(); shutdown() does.
void closeListenSocket(SocketHandle sock)
{
    shutdown(sock, SHUT_RDWR);
    close(sock);
}

void setReceiveTimeout(SocketHandle sock, int timeoutMs)
{
    timeval timeout = {};
    timeout.tv_sec = timeoutMs / 1000;
    timeout.tv_usec = (timeoutMs % 1000) * 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
}

// MSG_NOSIGNAL: a client that hung up yields EPIPE instead of killing the process with SIGPIPE.
int sendString(SocketHandle sock, const std::string& data)
{
    return static_cast<int>(send(sock, data.c_str(), data.length(), MSG_NOSIGNAL));
}

void nameThread(std::thread& thread)
{
    pthread_setname_np(thread.native_handle(), "HttpServerLoop");
}
#endif

const uintptr_t kInvalidSocket = static_cast<uintptr_t>(INVALID_SOCKET);

} // namespace

namespace httpServer {

HttpServer::HttpServer(std::weak_ptr<IHttpHandler> pHandler)
    : m_pHandler(pHandler)
    , m_bRunning(false)
    , m_listenSocket(kInvalidSocket)
{
}

HttpServer::~HttpServer()
{
    stop();
}

bool HttpServer::start(const HttpServerConfig& config)
{
    if (m_bRunning) {
        return false;
    }
    
    m_config = config;
    
    if (!startSockets()) {
        return false;
    }
    
    // Create socket
    SocketHandle listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket == INVALID_SOCKET) {
        stopSockets();
        return false;
    }
    m_listenSocket = static_cast<uintptr_t>(listenSocket);
    
    // Allow address reuse
    int opt = 1;
    setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&opt), sizeof(opt));
    
    // Bind
    sockaddr_in serverAddr = {};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(m_config.port);
    inet_pton(AF_INET, m_config.bindAddress.c_str(), &serverAddr.sin_addr);
    
    if (bind(listenSocket,
             reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) == SOCKET_ERROR) {
        closeSocket(listenSocket);
        m_listenSocket = kInvalidSocket;
        stopSockets();
        return false;
    }
    
    // Listen
    if (listen(listenSocket, m_config.maxConnections) == SOCKET_ERROR) {
        closeSocket(listenSocket);
        m_listenSocket = kInvalidSocket;
        stopSockets();
        return false;
    }
    
    // Start server thread
    m_bRunning = true;
    m_serverThread = std::thread(&HttpServer::serverLoop, this);
    nameThread(m_serverThread);
    
    return true;
}

void HttpServer::stop()
{
    if (!m_bRunning) {
        return;
    }
    
    m_bRunning = false;
    
    // Close listen socket to unblock accept()
    if (m_listenSocket != kInvalidSocket) {
        closeListenSocket(static_cast<SocketHandle>(m_listenSocket));
        m_listenSocket = kInvalidSocket;
    }
    
    // Wait for server thread to finish
    if (m_serverThread.joinable()) {
        m_serverThread.join();
    }
    
    stopSockets();
}

bool HttpServer::isRunning() const
{
    return m_bRunning;
}

uint16_t HttpServer::getPort() const
{
    return m_config.port;
}

std::string HttpServer::getUrl() const
{
    std::ostringstream url;
    // 0.0.0.0 means "all interfaces" — use localhost for the display URL
    const std::string& host = (m_config.bindAddress == "0.0.0.0" || m_config.bindAddress == "::")
                              ? "127.0.0.1" : m_config.bindAddress;
    url << "http://" << host << ":" << m_config.port << "/";
    return url.str();
}

void HttpServer::serverLoop()
{
    while (m_bRunning) {
        sockaddr_in clientAddr = {};
        socklen_t clientAddrLen = sizeof(clientAddr);
        
        SocketHandle clientSocket = accept(
            static_cast<SocketHandle>(m_listenSocket),
            reinterpret_cast<sockaddr*>(&clientAddr),
            &clientAddrLen
        );
        
        if (clientSocket == INVALID_SOCKET) {
            continue;
        }
        
        setReceiveTimeout(clientSocket, m_config.requestTimeoutMs);
        
        handleClient(static_cast<uintptr_t>(clientSocket));
    }
}

void HttpServer::handleClient(uintptr_t clientSocket)
{
    SocketHandle sock = static_cast<SocketHandle>(clientSocket);
    
    // Read request: headers first, then body based on Content-Length
    std::vector<char> buffer(8192);
    std::string rawRequest;

    // Read until we have the full header block (terminated by \r\n\r\n)
    while (true) {
        int bytesReceived = recv(sock, buffer.data(), static_cast<int>(buffer.size()) - 1, 0);
        if (bytesReceived <= 0) break;
        rawRequest.append(buffer.data(), bytesReceived);
        if (rawRequest.find("\r\n\r\n") != std::string::npos) break;
    }

    // Check Content-Length and read remaining body bytes if needed
    size_t headerEnd = rawRequest.find("\r\n\r\n");
    if (headerEnd != std::string::npos) {
        size_t bodyStart = headerEnd + 4;
        size_t contentLength = 0;

        // Parse Content-Length from headers (case-insensitive search)
        std::string headerBlock = rawRequest.substr(0, headerEnd);
        for (size_t pos = 0; pos < headerBlock.size(); ) {
            size_t lineEnd = headerBlock.find("\r\n", pos);
            if (lineEnd == std::string::npos) lineEnd = headerBlock.size();
            std::string line = headerBlock.substr(pos, lineEnd - pos);
            // Case-insensitive prefix match for "content-length:"
            if (line.size() > 15) {
                std::string lower = line.substr(0, 15);
                for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (lower == "content-length:") {
                    contentLength = std::stoull(line.substr(15));
                    break;
                }
            }
            pos = lineEnd + 2;
        }

        size_t bodyReceived = rawRequest.size() - bodyStart;
        while (bodyReceived < contentLength) {
            size_t toRead = contentLength - bodyReceived;
            if (toRead > buffer.size() - 1) toRead = buffer.size() - 1;
            int bytesReceived = recv(sock, buffer.data(), static_cast<int>(toRead), 0);
            if (bytesReceived <= 0) break;
            rawRequest.append(buffer.data(), bytesReceived);
            bodyReceived += bytesReceived;
        }
    }
    
    // Parse and handle request
    HttpResponse response = HttpResponse::error("Handler not available");
    
    if (!rawRequest.empty()) {
        HttpRequest request = parseRequest(rawRequest);
        
        if (auto pHandler = m_pHandler.lock()) {
            response = pHandler->handleRequest(request);
        }
    }
    
    // Send response
    std::string responseStr = response.build();
    sendString(sock, responseStr);
    
    // Close connection
    closeSocket(sock);
}

HttpRequest HttpServer::parseRequest(const std::string& rawRequest)
{
    HttpRequest request;
    std::istringstream stream(rawRequest);
    std::string line;
    
    // Parse request line: "GET /path?query HTTP/1.1"
    if (std::getline(stream, line)) {
        // Remove trailing \r if present
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        
        std::istringstream requestLine(line);
        std::string method, fullPath, version;
        requestLine >> method >> fullPath >> version;
        
        request.method = HttpRequest::stringToMethod(method);
        
        // Split path and query
        size_t queryPos = fullPath.find('?');
        if (queryPos != std::string::npos) {
            request.path = fullPath.substr(0, queryPos);
            request.query = fullPath.substr(queryPos + 1);
        } else {
            request.path = fullPath;
        }
    }
    
    // Parse headers
    while (std::getline(stream, line)) {
        // Remove trailing \r
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        
        // Empty line marks end of headers
        if (line.empty()) {
            break;
        }
        
        size_t colonPos = line.find(':');
        if (colonPos != std::string::npos) {
            std::string name = line.substr(0, colonPos);
            std::string value = line.substr(colonPos + 1);
            
            // Trim leading whitespace from value
            size_t valueStart = value.find_first_not_of(" \t");
            if (valueStart != std::string::npos) {
                value = value.substr(valueStart);
            }
            
            request.headers[name] = value;
        }
    }
    
    // Read body (rest of the request)
    std::ostringstream bodyStream;
    bodyStream << stream.rdbuf();
    request.body = bodyStream.str();
    
    return request;
}

} // namespace httpServer
