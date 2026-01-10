#include "HttpRequest.h"
#include <algorithm>
#include <sstream>

namespace httpServer {

std::string HttpRequest::getQueryParam(const std::string& key, const std::string& defaultValue) const
{
    if (query.empty()) {
        return defaultValue;
    }
    
    // Parse query string: key1=value1&key2=value2
    std::string searchKey = key + "=";
    size_t pos = 0;
    
    while (pos < query.length()) {
        size_t ampPos = query.find('&', pos);
        std::string pair = (ampPos != std::string::npos) 
            ? query.substr(pos, ampPos - pos) 
            : query.substr(pos);
        
        if (pair.find(searchKey) == 0) {
            return pair.substr(searchKey.length());
        }
        
        if (ampPos == std::string::npos) {
            break;
        }
        pos = ampPos + 1;
    }
    
    return defaultValue;
}

std::string HttpRequest::getHeader(const std::string& key, const std::string& defaultValue) const
{
    // Case-insensitive header lookup
    std::string lowerKey = key;
    std::transform(lowerKey.begin(), lowerKey.end(), lowerKey.begin(), ::tolower);
    
    for (const auto& [name, value] : headers) {
        std::string lowerName = name;
        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
        if (lowerName == lowerKey) {
            return value;
        }
    }
    
    return defaultValue;
}

const char* HttpRequest::methodToString(HttpMethod method)
{
    switch (method) {
        case HttpMethod::GET:     return "GET";
        case HttpMethod::POST:    return "POST";
        case HttpMethod::PUT:     return "PUT";
        case HttpMethod::DELETE:  return "DELETE";
        case HttpMethod::OPTIONS: return "OPTIONS";
        case HttpMethod::HEAD:    return "HEAD";
        default:                  return "UNKNOWN";
    }
}

HttpMethod HttpRequest::stringToMethod(const std::string& str)
{
    if (str == "GET")     return HttpMethod::GET;
    if (str == "POST")    return HttpMethod::POST;
    if (str == "PUT")     return HttpMethod::PUT;
    if (str == "DELETE")  return HttpMethod::DELETE;
    if (str == "OPTIONS") return HttpMethod::OPTIONS;
    if (str == "HEAD")    return HttpMethod::HEAD;
    return HttpMethod::UNKNOWN;
}

} // namespace httpServer
