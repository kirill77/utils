#pragma once

#include <string>
#include <map>

namespace httpServer {

// HTTP request method
enum class HttpMethod {
    GET,
    POST,
    PUT,
    DELETE,
    OPTIONS,
    HEAD,
    UNKNOWN
};

// Parsed HTTP request
struct HttpRequest {
    HttpMethod method = HttpMethod::UNKNOWN;
    std::string path;                                    // e.g., "/api/tests"
    std::string query;                                   // e.g., "id=5&name=test"
    std::map<std::string, std::string> headers;          // Header name -> value
    std::string body;                                    // Request body (for POST/PUT)
    
    // Helper to get a query parameter
    std::string getQueryParam(const std::string& key, const std::string& defaultValue = "") const;
    
    // Helper to get a header (case-insensitive)
    std::string getHeader(const std::string& key, const std::string& defaultValue = "") const;
    
    // Convert method enum to string
    static const char* methodToString(HttpMethod method);
    
    // Convert string to method enum
    static HttpMethod stringToMethod(const std::string& str);
};

} // namespace httpServer
