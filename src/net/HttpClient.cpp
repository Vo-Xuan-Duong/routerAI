#include "net/HttpClient.hpp"

#include <curl/curl.h>

#include <array>
#include <mutex>
#include <stdexcept>

namespace routerai {

namespace {

void ensureCurlInitialized() {
    static std::once_flag once;
    std::call_once(once, [] {
        if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
            throw std::runtime_error("Failed to initialize libcurl");
        }
    });
}

std::size_t writeBody(char* data, std::size_t size, std::size_t count, void* userData) {
    const std::size_t bytes = size * count;
    auto* body = static_cast<std::string*>(userData);
    body->append(data, bytes);
    return bytes;
}

HttpResponse perform(
    const std::string& url,
    const std::string* body,
    const std::map<std::string, std::string>& headers,
    long timeoutSeconds) {
    ensureCurlInitialized();

    CURL* curl = curl_easy_init();
    if (!curl) {
        throw std::runtime_error("Failed to create libcurl easy handle");
    }

    HttpResponse response;
    std::array<char, CURL_ERROR_SIZE> errorBuffer{};
    curl_slist* headerList = nullptr;
    for (const auto& [key, value] : headers) {
        const std::string line = key + ": " + value;
        headerList = curl_slist_append(headerList, line.c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeoutSeconds);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "routerAI/0.6.0");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeBody);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errorBuffer.data());
    if (headerList) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerList);
    }

    if (body) {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body->c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body->size()));
    }

    const CURLcode rc = curl_easy_perform(curl);
    if (rc != CURLE_OK) {
        response.error = !errorBuffer[0]
            ? curl_easy_strerror(rc)
            : std::string(errorBuffer.data());
    }

    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.statusCode);
    char* contentType = nullptr;
    curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &contentType);
    if (contentType) {
        response.contentType = contentType;
    }

    if (headerList) {
        curl_slist_free_all(headerList);
    }
    curl_easy_cleanup(curl);
    return response;
}

}  // namespace

HttpResponse HttpClient::get(
    const std::string& url,
    const std::map<std::string, std::string>& headers,
    long timeoutSeconds) {
    return perform(url, nullptr, headers, timeoutSeconds);
}

HttpResponse HttpClient::postJson(
    const std::string& url,
    const std::string& jsonBody,
    const std::map<std::string, std::string>& headers,
    long timeoutSeconds) {
    auto effectiveHeaders = headers;
    effectiveHeaders.emplace("Content-Type", "application/json");
    return perform(url, &jsonBody, effectiveHeaders, timeoutSeconds);
}

}  // namespace routerai
