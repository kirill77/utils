#pragma once

#include <string>
#include <vector>

struct HttpResponse
{
    int statusCode = 0;
    std::string body;
    std::string errorMessage;
};

class HttpClient
{
public:
    // Simple GET request using WinHTTP. URL must be absolute (http/https).
    static HttpResponse get(const std::wstring &url,
                            const std::vector<std::pair<std::wstring, std::wstring>> &headers = {});
};


