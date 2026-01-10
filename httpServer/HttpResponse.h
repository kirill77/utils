#pragma once

#include <string>
#include <map>
#include <vector>

namespace httpServer {

// Common HTTP status codes
enum class HttpStatus {
    OK = 200,
    Created = 201,
    NoContent = 204,
    BadRequest = 400,
    NotFound = 404,
    MethodNotAllowed = 405,
    InternalServerError = 500
};

// HTTP response builder
class HttpResponse {
public:
    HttpResponse();
    explicit HttpResponse(HttpStatus status);
    
    // Set status code
    HttpResponse& setStatus(HttpStatus status);
    HttpResponse& setStatus(int statusCode);
    
    // Set headers
    HttpResponse& setHeader(const std::string& name, const std::string& value);
    HttpResponse& setContentType(const std::string& contentType);
    
    // Set body
    HttpResponse& setBody(const std::string& body);
    HttpResponse& setBody(const char* data, size_t length);
    
    // Convenience methods for common responses
    static HttpResponse json(const std::string& jsonBody);
    static HttpResponse html(const std::string& htmlBody);
    static HttpResponse text(const std::string& textBody);
    static HttpResponse file(const std::string& content, const std::string& contentType);
    static HttpResponse notFound(const std::string& message = "Not Found");
    static HttpResponse badRequest(const std::string& message = "Bad Request");
    static HttpResponse error(const std::string& message = "Internal Server Error");
    
    // Build the raw HTTP response string
    std::string build() const;
    
    // Getters
    int getStatusCode() const { return m_statusCode; }
    const std::string& getBody() const { return m_body; }

private:
    int m_statusCode;
    std::map<std::string, std::string> m_headers;
    std::string m_body;
    
    static const char* getStatusText(int statusCode);
};

} // namespace httpServer
