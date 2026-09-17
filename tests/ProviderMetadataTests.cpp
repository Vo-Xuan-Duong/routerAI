#include "providers/antigravity/AntigravityApiClient.hpp"
#include "providers/zai/ZaiClient.hpp"
#include "providers/zai/ZaiProvider.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

bool contains(const std::vector<std::string>& values, const std::string& value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

}  // namespace

int main() {
    try {
        require(
            routerai::AntigravityApiClient::endpoint() ==
                "https://generativelanguage.googleapis.com/v1beta/interactions",
            "Antigravity interactions endpoint changed unexpectedly");
        require(
            routerai::AntigravityApiClient::modelsEndpoint() ==
                "https://generativelanguage.googleapis.com/v1beta/openai/models",
            "Gemini OpenAI-compatible models endpoint mismatch");
        require(
            !routerai::AntigravityApiClient::supportedAgentModels().empty(),
            "Antigravity documented model set must not be empty");

        require(
            routerai::ZaiProvider::generalBaseUrl() ==
                "https://api.z.ai/api/paas/v4",
            "Z.ai General API base URL mismatch");
        require(
            routerai::ZaiProvider::codingBaseUrl() ==
                "https://api.z.ai/api/coding/paas/v4",
            "Z.ai Coding Plan base URL mismatch");

        const auto generalModels = routerai::ZaiClient::documentedChatModels();
        require(contains(generalModels, "glm-5.2"), "General API model list must contain glm-5.2");
        require(contains(generalModels, "glm-5.1"), "General API model list must contain glm-5.1");
        require(contains(generalModels, "glm-4.7"), "General API model list must contain glm-4.7");

        const auto codingModels = routerai::ZaiClient::documentedCodingPlanModels();
        require(codingModels.size() == 5, "Coding Plan model list should match the documented five-model boundary");
        require(contains(codingModels, "glm-5.2"), "Coding Plan must include glm-5.2");
        require(contains(codingModels, "glm-5.1"), "Coding Plan must include glm-5.1");
        require(contains(codingModels, "glm-5-turbo"), "Coding Plan must include glm-5-turbo");
        require(contains(codingModels, "glm-4.7"), "Coding Plan must include glm-4.7");
        require(contains(codingModels, "glm-4.5-air"), "Coding Plan must include glm-4.5-air");
        require(!contains(codingModels, "glm-5"), "Coding Plan must not inherit unsupported General API models");

        std::cout << "ProviderMetadataTests: OK\n";
    } catch (const std::exception& exception) {
        std::cerr << "ProviderMetadataTests: FAILED: " << exception.what() << '\n';
        return 1;
    }
    return 0;
}
