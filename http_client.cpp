#include "http_client.h"

#include <curl/curl.h>

namespace
{
    size_t WriteCallback(char* ptr, size_t size, size_t nmemb, void* userdata)
    {
        std::string* out = static_cast<std::string*>(userdata);
        out->append(ptr, size * nmemb);
        return size * nmemb;
    }

    HttpResponse Perform(const std::string& url, const std::vector<std::string>& headers,
        const char* method, const std::string* body)
    {
        HttpResponse response;

        CURL* curl = curl_easy_init();
        if (!curl)
        {
            response.error = "curl_easy_init failed";
            return response;
        }

        curl_slist* headerList = nullptr;
        for (const std::string& header : headers)
            headerList = curl_slist_append(headerList, header.c_str());

        std::string responseBody;

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBody);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 3000L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 2000L);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

        if (headerList)
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerList);

        if (method != nullptr)
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method);

        if (body != nullptr)
        {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body->c_str());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body->size());
        }

        CURLcode result = curl_easy_perform(curl);

        if (result != CURLE_OK)
        {
            response.error = curl_easy_strerror(result);
        }
        else
        {
            long statusCode = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &statusCode);
            response.statusCode = statusCode;
            response.body = std::move(responseBody);
            response.ok = statusCode >= 200 && statusCode < 300;
        }

        if (headerList)
            curl_slist_free_all(headerList);
        curl_easy_cleanup(curl);

        return response;
    }
}

void HttpGlobalInit()
{
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

void HttpGlobalCleanup()
{
    curl_global_cleanup();
}

HttpResponse HttpGet(const std::string& url, const std::vector<std::string>& headers)
{
    return Perform(url, headers, nullptr, nullptr);
}

HttpResponse HttpPostJson(const std::string& url, const std::string& jsonBody, const std::vector<std::string>& headers)
{
    std::vector<std::string> allHeaders = headers;
    allHeaders.push_back("Content-Type: application/json");
    return Perform(url, allHeaders, "POST", &jsonBody);
}

HttpResponse HttpPutJson(const std::string& url, const std::string& jsonBody, const std::vector<std::string>& headers)
{
    std::vector<std::string> allHeaders = headers;
    allHeaders.push_back("Content-Type: application/json");
    return Perform(url, allHeaders, "PUT", &jsonBody);
}
