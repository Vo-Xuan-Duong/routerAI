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

        const auto antigravityModels =
            routerai::AntigravityApiClient::supportedAgentModels();
        require(!antigravityModels.empty(), "Antigravity documented model set must not be empty");

        require(
            routerai::ZaiProvider::generalBaseUrl() ==
                "https://api.z.ai/api/paas/v4",
            "Z.ai General API base URL mismatch");
        require(
            routerai::ZaiProvider::codingBaseUrl() ==
                "https://api.z.ai/api/coding/paas/v4",
            "Z.ai Coding Plan base URL mismatch");

        const auto zaiModels = routerai::ZaiClient::documentedChatModels();
        require(contains(zaiModels, "glm-5.1"), "documented Z.ai model list must contain glm-5.1");
        require(contains(zaiModels, "glm-4.7"), "documented Z.ai model list must contain glm-4.7");
        require(!contains(zaiModels, "glm-5.2"), "undocumented model guesses must not enter the static model list");

        std::cout << "ProviderMetadataTests: OK\n";
    } catch (const std::exception& exception) {
        std::cerr << "ProviderMetadataTests: FAILED: " << exception.what() << '\n';
        return 1;
    }
    return 0;
}
