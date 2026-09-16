#pragma once

#include <map>
#include <string>

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
};

}  // namespace routerai
