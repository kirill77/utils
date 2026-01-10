#include "HttpResponse.h"
#include <sstream>

namespace httpServer {

HttpResponse::HttpResponse()
    : m_statusCode(200)
{
    m_headers["Server"] = "VerdictHttpServer/1.0";
    m_headers["Connection"] = "close";
}

HttpResponse::HttpResponse(HttpStatus status)
    : HttpResponse()
{
    m_statusCode = static_cast<int>(status);
}

HttpResponse& HttpResponse::setStatus(HttpStatus status)
{
    m_statusCode = static_cast<int>(status);
    return *this;
}

HttpResponse& HttpResponse::setStatus(int statusCode)
{
    m_statusCode = statusCode;
    return *this;
}

HttpResponse& HttpResponse::setHeader(const std::string& name, const std::string& value)
{
    m_headers[name] = value;
    return *this;
}

HttpResponse& HttpResponse::setContentType(const std::string& contentType)
{
    m_headers["Content-Type"] = contentType;
    return *this;
}

HttpResponse& HttpResponse::setBody(const std::string& body)
{
    m_body = body;
    m_headers["Content-Length"] = std::to_string(m_body.length());
    return *this;
}

HttpResponse& HttpResponse::setBody(const char* data, size_t length)
{
    m_body.assign(data, length);
    m_headers["Content-Length"] = std::to_string(length);
    return *this;
}

HttpResponse HttpResponse::json(const std::string& jsonBody)
{
    HttpResponse response(HttpStatus::OK);
    response.setContentType("application/json");
    response.setBody(jsonBody);
    return response;
}

HttpResponse HttpResponse::html(const std::string& htmlBody)
{
    HttpResponse response(HttpStatus::OK);
    response.setContentType("text/html; charset=utf-8");
    response.setBody(htmlBody);
    return response;
}

HttpResponse HttpResponse::text(const std::string& textBody)
{
    HttpResponse response(HttpStatus::OK);
    response.setContentType("text/plain; charset=utf-8");
    response.setBody(textBody);
    return response;
}

HttpResponse HttpResponse::file(const std::string& content, const std::string& contentType)
{
    HttpResponse response(HttpStatus::OK);
    response.setContentType(contentType);
    response.setBody(content);
    return response;
}

HttpResponse HttpResponse::notFound(const std::string& message)
{
    HttpResponse response(HttpStatus::NotFound);
    response.setContentType("application/json");
    response.setBody("{\"error\":\"" + message + "\"}");
    return response;
}

HttpResponse HttpResponse::badRequest(const std::string& message)
{
    HttpResponse response(HttpStatus::BadRequest);
    response.setContentType("application/json");
    response.setBody("{\"error\":\"" + message + "\"}");
    return response;
}

HttpResponse HttpResponse::error(const std::string& message)
{
    HttpResponse response(HttpStatus::InternalServerError);
    response.setContentType("application/json");
    response.setBody("{\"error\":\"" + message + "\"}");
    return response;
}

std::string HttpResponse::build() const
{
    std::ostringstream response;
    
    // Status line
    response << "HTTP/1.1 " << m_statusCode << " " << getStatusText(m_statusCode) << "\r\n";
    
    // Headers
    for (const auto& [name, value] : m_headers) {
        response << name << ": " << value << "\r\n";
    }
    
    // Empty line separating headers from body
    response << "\r\n";
    
    // Body
    response << m_body;
    
    return response.str();
}

const char* HttpResponse::getStatusText(int statusCode)
{
    switch (statusCode) {
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 304: return "Not Modified";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        case 503: return "Service Unavailable";
        default:  return "Unknown";
    }
}

} // namespace httpServer
