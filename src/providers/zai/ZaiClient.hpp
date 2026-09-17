#pragma once

#include "net/HttpClient.hpp"

#include <string>
#include <vector>

namespace routerai {

class ZaiClient {
public:
    static HttpResponse chatCompletions(
        const std::string& apiKey,
        const std::string& jsonBody,
        long timeoutSeconds = 120);

    static std::vector<std::string> documentedChatModels();
    static std::vector<std::string> documentedCodingPlanModels();
};

}  // namespace routerai
