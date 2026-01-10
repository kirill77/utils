#include "HttpServer.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "Ws2_32.lib")

#include <sstream>
#include <vector>

namespace httpServer {

HttpServer::HttpServer(std::weak_ptr<IHttpHandler> pHandler)
    : m_pHandler(pHandler)
    , m_bRunning(false)
    , m_listenSocket(INVALID_SOCKET)
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
    
    // Initialize Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        return false;
    }
    
    // Create socket
    m_listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_listenSocket == INVALID_SOCKET) {
        WSACleanup();
        return false;
    }
    
    // Allow address reuse
    int opt = 1;
    setsockopt(static_cast<SOCKET>(m_listenSocket), SOL_SOCKET, SO_REUSEADDR, 
               reinterpret_cast<const char*>(&opt), sizeof(opt));
    
    // Bind
    sockaddr_in serverAddr = {};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(m_config.port);
    inet_pton(AF_INET, m_config.bindAddress.c_str(), &serverAddr.sin_addr);
    
    if (bind(static_cast<SOCKET>(m_listenSocket), 
             reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) == SOCKET_ERROR) {
        closesocket(static_cast<SOCKET>(m_listenSocket));
        WSACleanup();
        return false;
    }
    
    // Listen
    if (listen(static_cast<SOCKET>(m_listenSocket), m_config.maxConnections) == SOCKET_ERROR) {
        closesocket(static_cast<SOCKET>(m_listenSocket));
        WSACleanup();
        return false;
    }
    
    // Start server thread
    m_bRunning = true;
    m_serverThread = std::thread(&HttpServer::serverLoop, this);
    
    return true;
}

void HttpServer::stop()
{
    if (!m_bRunning) {
        return;
    }
    
    m_bRunning = false;
    
    // Close listen socket to unblock accept()
    if (m_listenSocket != INVALID_SOCKET) {
        closesocket(static_cast<SOCKET>(m_listenSocket));
        m_listenSocket = INVALID_SOCKET;
    }
    
    // Wait for server thread to finish
    if (m_serverThread.joinable()) {
        m_serverThread.join();
    }
    
    WSACleanup();
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
    url << "http://" << m_config.bindAddress << ":" << m_config.port << "/";
    return url.str();
}

void HttpServer::serverLoop()
{
    while (m_bRunning) {
        sockaddr_in clientAddr = {};
        int clientAddrLen = sizeof(clientAddr);
        
        SOCKET clientSocket = accept(
            static_cast<SOCKET>(m_listenSocket),
            reinterpret_cast<sockaddr*>(&clientAddr),
            &clientAddrLen
        );
        
        if (clientSocket == INVALID_SOCKET) {
            continue;
        }
        
        // Set receive timeout
        DWORD timeout = m_config.requestTimeoutMs;
        setsockopt(clientSocket, SOL_SOCKET, SO_RCVTIMEO, 
                   reinterpret_cast<const char*>(&timeout), sizeof(timeout));
        
        handleClient(static_cast<uintptr_t>(clientSocket));
    }
}

void HttpServer::handleClient(uintptr_t clientSocket)
{
    SOCKET sock = static_cast<SOCKET>(clientSocket);
    
    // Read request
    std::vector<char> buffer(8192);
    std::string rawRequest;
    
    int bytesReceived = recv(sock, buffer.data(), static_cast<int>(buffer.size()) - 1, 0);
    if (bytesReceived > 0) {
        buffer[bytesReceived] = '\0';
        rawRequest = buffer.data();
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
    send(sock, responseStr.c_str(), static_cast<int>(responseStr.length()), 0);
    
    // Close connection
    closesocket(sock);
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
