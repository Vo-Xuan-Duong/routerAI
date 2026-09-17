#pragma once

#include <functional>
#include <map>
#include <string>
#include <string_view>

namespace routerai {

struct HttpResponse {
    long statusCode{0};
    std::string body;
    std::string contentType;
    std::string error;

    bool succeeded() const noexcept {
        return error.empty() && statusCode >= 200 && statusCode < 300;
    }
};

using HttpStreamCallback = std::function<bool(std::string_view)>;

class HttpClient {
public:
    static HttpResponse get(
        const std::string& url,
        const std::map<std::string, std::string>& headers = {},
        long timeoutSeconds = 30);

    static HttpResponse postJson(
        const std::string& url,
        const std::string& jsonBody,
        const std::map<std::string, std::string>& headers = {},
        long timeoutSeconds = 120);

    // Streams a successful HTTP response body to `onChunk`. Non-2xx bodies
    // remain buffered in HttpResponse::body so callers can inspect provider
    // errors without forwarding them as stream data.
    static HttpResponse postJsonStream(
        const std::string& url,
        const std::string& jsonBody,
        const std::map<std::string, std::string>& headers,
        const HttpStreamCallback& onChunk,
        long timeoutSeconds = 120);
};

}  // namespace routerai
