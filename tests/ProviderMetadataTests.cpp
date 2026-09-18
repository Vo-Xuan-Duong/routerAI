#include "providers/antigravity/AntigravityApiClient.hpp"
#include "providers/antigravity/AntigravityCli.hpp"
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

        const std::string tsvOutput =
            "Gemini Models\tWeekly Limit Remaining\t50%\t2026-09-23T02:26:28Z\n"
            "Gemini Models\tFive Hour Limit Remaining\t57%\t2026-09-18T17:56:26Z\n"
            "Claude and GPT models\tWeekly Limit Remaining\t100%\t2026-09-25T16:17:25Z\n"
            "Claude and GPT models\tFive Hour Limit Remaining\t100%\t2026-09-18T21:17:25Z\n";

        const auto snapshot = routerai::AntigravityCli::parseQuotaOutput(tsvOutput);
        require(snapshot.buckets.size() == 4, "TSV parse should return 4 buckets");
        require(snapshot.ordinaryUsageAllowed.value_or(false), "Ordinary usage should be allowed");
        require(snapshot.buckets[0].windows.size() == 1, "Bucket 0 should have 1 window");
        require(snapshot.buckets[0].windows[0].usedPercent == 50.0, "Bucket 0 usedPercent should be 50.0");
        require(snapshot.buckets[1].windows[0].usedPercent == 43.0, "Bucket 1 usedPercent should be 43.0");
        require(snapshot.buckets[2].windows[0].usedPercent == 0.0, "Bucket 2 usedPercent should be 0.0");

        const std::string textOutput =
            "Gemini Models: 40% remaining (refreshes in 3d)\n"
            "Claude Models: 100% remaining\n";
        const auto textSnapshot = routerai::AntigravityCli::parseQuotaOutput(textOutput);
        require(textSnapshot.buckets.size() == 2, "Text parse should return 2 buckets");
        require(textSnapshot.buckets[0].windows[0].usedPercent == 60.0, "Text bucket 0 usedPercent should be 60.0");

        std::cout << "ProviderMetadataTests: OK\n";
    } catch (const std::exception& exception) {
        std::cerr << "ProviderMetadataTests: FAILED: " << exception.what() << '\n';
        return 1;
    }
    return 0;
}
