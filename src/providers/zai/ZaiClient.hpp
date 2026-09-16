#pragma once

#include "net/HttpClient.hpp"

#include <string>

namespace routerai {

class ZaiClient {
public:
    static HttpResponse chatCompletions(
        const std::string& apiKey,
        const std::string& jsonBody,
        long timeoutSeconds = 120);
};

}  // namespace routerai
