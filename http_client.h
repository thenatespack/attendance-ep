#pragma once

#include <string>
#include <vector>

struct HttpResponse
{
    bool ok = false;          // true if the transport succeeded and status is 2xx
    long statusCode = 0;
    std::string body;
    std::string error;        // transport-level error (e.g. connection refused)
};

// Must be called once before any HttpGet/HttpPostJson/HttpPutJson calls,
// and HttpGlobalCleanup() once at shutdown.
void HttpGlobalInit();
void HttpGlobalCleanup();

HttpResponse HttpGet(const std::string& url, const std::vector<std::string>& headers = {});
HttpResponse HttpPostJson(const std::string& url, const std::string& jsonBody, const std::vector<std::string>& headers = {});
HttpResponse HttpPutJson(const std::string& url, const std::string& jsonBody, const std::vector<std::string>& headers = {});
