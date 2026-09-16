#pragma once

#include "net/HttpClient.hpp"

#include <string>

namespace routerai {

class AntigravityApiClient {
public:
    static HttpResponse createInteraction(
        const std::string& apiKey,
        const std::string& jsonBody,
        long timeoutSeconds = 300);

    static std::string endpoint();
    static std::string agentName();
};

}  // namespace routerai
