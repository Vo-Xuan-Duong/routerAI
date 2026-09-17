#pragma once

#include "net/HttpClient.hpp"

#include <string>
#include <vector>

namespace routerai {

class AntigravityApiClient {
public:
    static HttpResponse createInteraction(
        const std::string& apiKey,
        const std::string& jsonBody,
        long timeoutSeconds = 300);

    static HttpResponse createInteractionStream(
        const std::string& apiKey,
        const std::string& jsonBody,
        const HttpStreamCallback& onChunk,
        long timeoutSeconds = 300);

    static HttpResponse listModels(
        const std::string& apiKey,
        long timeoutSeconds = 30);

    static std::vector<std::string> supportedAgentModels();

    static std::string endpoint();
    static std::string streamingEndpoint();
    static std::string modelsEndpoint();
    static std::string agentName();
};

}  // namespace routerai
