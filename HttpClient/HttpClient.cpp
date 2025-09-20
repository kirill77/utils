#include "HttpClient.h"
#include <windows.h>
#include <winhttp.h>
#include <vector>
#include <string>

#pragma comment(lib, "winhttp.lib")

static std::wstring getHostFromUrl(const std::wstring &url, INTERNET_PORT &port, bool &isHttps, std::wstring &path)
{
    URL_COMPONENTS uc;
    memset(&uc, 0, sizeof(uc));
    uc.dwStructSize = sizeof(uc);

    std::vector<wchar_t> host(256);
    std::vector<wchar_t> urlPath(2048);
    uc.lpszHostName = host.data();
    uc.dwHostNameLength = (DWORD)host.size();
    uc.lpszUrlPath = urlPath.data();
    uc.dwUrlPathLength = (DWORD)urlPath.size();

    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc))
        return L"";

    isHttps = (uc.nScheme == INTERNET_SCHEME_HTTPS);
    port = uc.nPort;
    path.assign(uc.lpszUrlPath, uc.dwUrlPathLength);
    return std::wstring(uc.lpszHostName, uc.dwHostNameLength);
}

HttpResponse HttpClient::get(const std::wstring &url,
                             const std::vector<std::pair<std::wstring, std::wstring>> &headers)
{
    HttpResponse resp;
    INTERNET_PORT port = 0;
    bool isHttps = false;
    std::wstring path;
    std::wstring host = getHostFromUrl(url, port, isHttps, path);
    if (host.empty())
    {
        resp.errorMessage = "Failed to parse URL";
        return resp;
    }

    HINTERNET hSession = WinHttpOpen(L"worm/1.0",
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession)
    {
        resp.errorMessage = "WinHttpOpen failed";
        return resp;
    }

    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), port, 0);
    if (!hConnect)
    {
        WinHttpCloseHandle(hSession);
        resp.errorMessage = "WinHttpConnect failed";
        return resp;
    }

    DWORD flags = isHttps ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(), NULL,
                                            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                            flags);
    if (!hRequest)
    {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        resp.errorMessage = "WinHttpOpenRequest failed";
        return resp;
    }

    for (const auto &h : headers)
    {
        std::wstring headerLine = h.first + L": " + h.second + L"\r\n";
        WinHttpAddRequestHeaders(hRequest, headerLine.c_str(), (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);
    }

    if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
    {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        resp.errorMessage = "WinHttpSendRequest failed";
        return resp;
    }

    if (!WinHttpReceiveResponse(hRequest, NULL))
    {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        resp.errorMessage = "WinHttpReceiveResponse failed";
        return resp;
    }

    DWORD statusCode = 0; DWORD statusCodeSize = sizeof(statusCode);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusCodeSize, WINHTTP_NO_HEADER_INDEX);
    resp.statusCode = (int)statusCode;

    DWORD dwSize = 0;
    do
    {
        dwSize = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &dwSize))
            break;
        if (dwSize == 0)
            break;
        std::vector<char> buffer(dwSize);
        DWORD dwRead = 0;
        if (!WinHttpReadData(hRequest, buffer.data(), dwSize, &dwRead))
            break;
        resp.body.append(buffer.data(), buffer.data() + dwRead);
    }
    while (dwSize > 0);

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return resp;
}


