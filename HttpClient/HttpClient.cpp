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
                             const std::vector<std::pair<std::wstring, std::wstring>> &headers,
                             int receiveTimeoutMs)
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

    HINTERNET hSession = WinHttpOpen(L"WinHttpClient/1.0",
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession)
    {
        resp.errorMessage = "WinHttpOpen failed";
        return resp;
    }

    // WinHTTP's default receive-headers timeout is 30 s, which is too
    // short for endpoints that compute responses server-side (e.g.
    // SABIO-RK's searchKineticLaws/sbml takes ~75 s time-to-first-byte
    // for a per-organism kcat query).  Callers opt in by passing a
    // larger receiveTimeoutMs; static-file callers (PaxDB, GeneWiki)
    // leave it at 0 and inherit WinHTTP's default.  WinHttpSetOption
    // with WINHTTP_OPTION_RECEIVE_TIMEOUT modifies just the receive
    // timeout — resolve / connect / send keep their WinHTTP defaults.
    if (receiveTimeoutMs > 0)
    {
        DWORD t = static_cast<DWORD>(receiveTimeoutMs);
        WinHttpSetOption(hSession, WINHTTP_OPTION_RECEIVE_TIMEOUT,
                         &t, sizeof(t));
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


