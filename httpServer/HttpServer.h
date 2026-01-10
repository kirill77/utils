#pragma once

#include "IHttpHandler.h"
#include <memory>
#include <string>
#include <atomic>
#include <thread>

namespace httpServer {

// Configuration for the HTTP server
struct HttpServerConfig {
    uint16_t port = 8080;           // Port to listen on
    std::string bindAddress = "127.0.0.1";  // Address to bind (default: localhost only)
    int maxConnections = 10;        // Maximum concurrent connections
    int requestTimeoutMs = 30000;   // Request timeout in milliseconds
};

// Simple HTTP server for serving web UI and REST API
// Single-threaded accept loop with synchronous request handling
class HttpServer {
public:
    explicit HttpServer(std::weak_ptr<IHttpHandler> pHandler);
    ~HttpServer();
    
    // Delete copy/move
    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;
    HttpServer(HttpServer&&) = delete;
    HttpServer& operator=(HttpServer&&) = delete;
    
    // Start the server (non-blocking - runs in background thread)
    bool start(const HttpServerConfig& config = {});
    
    // Stop the server and wait for it to shut down
    void stop();
    
    // Check if the server is running
    bool isRunning() const;
    
    // Get the port the server is listening on
    uint16_t getPort() const;
    
    // Get the full URL to access the server
    std::string getUrl() const;

private:
    void serverLoop();
    void handleClient(uintptr_t clientSocket);
    HttpRequest parseRequest(const std::string& rawRequest);
    
    std::weak_ptr<IHttpHandler> m_pHandler;
    HttpServerConfig m_config;
    
    std::atomic<bool> m_bRunning;
    std::thread m_serverThread;
    uintptr_t m_listenSocket;
};

} // namespace httpServer
