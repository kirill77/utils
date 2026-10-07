// HttpClient on libcurl, for non-Windows platforms (Windows builds HttpClient.cpp, on WinHTTP).
#include "HttpClient.h"
#include <curl/curl.h>
#include <cstdint>
#include <string>

namespace
{
// Same defaults WinHTTP applies: 60 s to connect, 30 s without data before a receive gives up.
const long CONNECT_TIMEOUT_MS = 60000;
const int DEFAULT_RECEIVE_TIMEOUT_MS = 30000;

// wchar_t is UTF-32 on the platforms that build this file.
std::string toUtf8(const std::wstring &s)
{
    std::string out;
    for (wchar_t wc : s)
    {
        uint32_t c = static_cast<uint32_t>(wc);
        if (c < 0x80)
        {
            out += static_cast<char>(c);
        }
        else if (c < 0x800)
        {
            out += static_cast<char>(0xC0 | (c >> 6));
            out += static_cast<char>(0x80 | (c & 0x3F));
        }
        else if (c < 0x10000)
        {
            out += static_cast<char>(0xE0 | (c >> 12));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        }
        else
        {
            out += static_cast<char>(0xF0 | (c >> 18));
            out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        }
    }
    return out;
}

size_t appendToBody(char *pData, size_t size, size_t count, void *pBody)
{
    static_cast<std::string *>(pBody)->append(pData, size * count);
    return size * count;
}
}

HttpResponse HttpClient::get(const std::wstring &url,
                             const std::vector<std::pair<std::wstring, std::wstring>> &headers,
                             int receiveTimeoutMs)
{
    // curl_global_init is not thread-safe; a function-local static runs it exactly once.
    static const CURLcode s_globalInit = curl_global_init(CURL_GLOBAL_DEFAULT);

    HttpResponse resp;
    if (s_globalInit != CURLE_OK)
    {
        resp.errorMessage = "curl_global_init failed";
        return resp;
    }

    CURL *pCurl = curl_easy_init();
    if (!pCurl)
    {
        resp.errorMessage = "curl_easy_init failed";
        return resp;
    }

    std::string sUrl = toUtf8(url);
    curl_slist *pHeaders = nullptr;
    for (const auto &h : headers)
    {
        pHeaders = curl_slist_append(pHeaders, toUtf8(h.first + L": " + h.second).c_str());
    }

    // curl has no per-receive timeout; "less than 1 byte/s for N seconds" is the equivalent.
    // It counts whole seconds, so round up: 0 would switch the check off.
    long receiveTimeoutMsUsed = receiveTimeoutMs > 0 ? receiveTimeoutMs : DEFAULT_RECEIVE_TIMEOUT_MS;
    long receiveTimeoutSec = (receiveTimeoutMsUsed + 999) / 1000;

    curl_easy_setopt(pCurl, CURLOPT_URL, sUrl.c_str());
    curl_easy_setopt(pCurl, CURLOPT_HTTPHEADER, pHeaders);
    curl_easy_setopt(pCurl, CURLOPT_USERAGENT, "HttpClient/1.0");
    curl_easy_setopt(pCurl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(pCurl, CURLOPT_CONNECTTIMEOUT_MS, CONNECT_TIMEOUT_MS);
    curl_easy_setopt(pCurl, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(pCurl, CURLOPT_LOW_SPEED_TIME, receiveTimeoutSec);
    curl_easy_setopt(pCurl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(pCurl, CURLOPT_WRITEFUNCTION, appendToBody);
    curl_easy_setopt(pCurl, CURLOPT_WRITEDATA, &resp.body);

    CURLcode result = curl_easy_perform(pCurl);
    if (result == CURLE_OK)
    {
        long statusCode = 0;
        curl_easy_getinfo(pCurl, CURLINFO_RESPONSE_CODE, &statusCode);
        resp.statusCode = static_cast<int>(statusCode);
    }
    else
    {
        resp.errorMessage = curl_easy_strerror(result);
    }

    curl_slist_free_all(pHeaders);
    curl_easy_cleanup(pCurl);
    return resp;
}
