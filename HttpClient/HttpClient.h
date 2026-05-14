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
    //
    // receiveTimeoutMs > 0 overrides WinHTTP's 30 s default receive-headers
    // timeout — pass a larger value when the server is known to take long
    // to compose its first byte (e.g. on-demand exports built per request).
    // 0 keeps the WinHTTP default; most callers (static files / quick CGI)
    // need nothing more.
    static HttpResponse get(const std::wstring &url,
                            const std::vector<std::pair<std::wstring, std::wstring>> &headers = {},
                            int receiveTimeoutMs = 0);
};


