#include "net/HttpClient.hpp"
#include "Version.hpp"

#include <curl/curl.h>

#include <array>
#include <cstdio>
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

struct StreamState {
    const HttpStreamCallback* callback{nullptr};
    std::string errorBody;
    long statusCode{0};
    bool callbackCancelled{false};
};

std::size_t streamHeader(char* data, std::size_t size, std::size_t count, void* userData) {
    const std::size_t bytes = size * count;
    auto* state = static_cast<StreamState*>(userData);
    const std::string line(data, bytes);

    if (line.starts_with("HTTP/")) {
        long status = 0;
        if (std::sscanf(line.c_str(), "HTTP/%*s %ld", &status) == 1) {
            state->statusCode = status;
        }
    }
    return bytes;
}

std::size_t streamBody(char* data, std::size_t size, std::size_t count, void* userData) {
    const std::size_t bytes = size * count;
    auto* state = static_cast<StreamState*>(userData);

    if (state->statusCode >= 200 && state->statusCode < 300 && state->callback) {
        if (!(*state->callback)(std::string_view(data, bytes))) {
            state->callbackCancelled = true;
            return 0;
        }
    } else {
        state->errorBody.append(data, bytes);
    }
    return bytes;
}

void applyCommonOptions(
    CURL* curl,
    const std::string& url,
    long timeoutSeconds,
    std::array<char, CURL_ERROR_SIZE>& errorBuffer) {
    const std::string userAgent = std::string("routerAI/") + kVersion;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeoutSeconds);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, userAgent.c_str());
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errorBuffer.data());
}

curl_slist* makeHeaders(const std::map<std::string, std::string>& headers) {
    curl_slist* headerList = nullptr;
    for (const auto& [key, value] : headers) {
        const std::string line = key + ": " + value;
        headerList = curl_slist_append(headerList, line.c_str());
    }
    return headerList;
}

HttpResponse finalizeResponse(
    CURL* curl,
    CURLcode rc,
    const std::array<char, CURL_ERROR_SIZE>& errorBuffer) {
    HttpResponse response;
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
    return response;
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
    curl_slist* headerList = makeHeaders(headers);

    applyCommonOptions(curl, url, timeoutSeconds, errorBuffer);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeBody);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
    if (headerList) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerList);
    }

    if (body) {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body->c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body->size()));
    }

    const CURLcode rc = curl_easy_perform(curl);
    HttpResponse metadata = finalizeResponse(curl, rc, errorBuffer);
    response.statusCode = metadata.statusCode;
    response.contentType = std::move(metadata.contentType);
    response.error = std::move(metadata.error);

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

HttpResponse HttpClient::postJsonStream(
    const std::string& url,
    const std::string& jsonBody,
    const std::map<std::string, std::string>& headers,
    const HttpStreamCallback& onChunk,
    long timeoutSeconds) {
    ensureCurlInitialized();

    CURL* curl = curl_easy_init();
    if (!curl) {
        throw std::runtime_error("Failed to create libcurl easy handle");
    }

    auto effectiveHeaders = headers;
    effectiveHeaders.emplace("Content-Type", "application/json");
    curl_slist* headerList = makeHeaders(effectiveHeaders);
    std::array<char, CURL_ERROR_SIZE> errorBuffer{};
    StreamState state;
    state.callback = &onChunk;

    applyCommonOptions(curl, url, timeoutSeconds, errorBuffer);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonBody.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(jsonBody.size()));
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, streamHeader);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &state);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, streamBody);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &state);
    if (headerList) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerList);
    }

    const CURLcode rc = curl_easy_perform(curl);
    HttpResponse response = finalizeResponse(curl, rc, errorBuffer);
    response.body = std::move(state.errorBody);

    if (state.callbackCancelled && rc == CURLE_WRITE_ERROR) {
        response.error = "stream callback cancelled";
    }

    if (headerList) {
        curl_slist_free_all(headerList);
    }
    curl_easy_cleanup(curl);
    return response;
}

}  // namespace routerai
