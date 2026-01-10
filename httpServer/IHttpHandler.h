#pragma once

#include "HttpRequest.h"
#include "HttpResponse.h"
#include <memory>

namespace httpServer {

// Interface for handling HTTP requests
// Implement this to define your application's HTTP endpoints
struct IHttpHandler {
    virtual ~IHttpHandler() = default;
    
    // Handle an incoming HTTP request and return a response
    // Called from the server's worker thread - implementation must be thread-safe
    virtual HttpResponse handleRequest(const HttpRequest& request) = 0;
};

} // namespace httpServer
