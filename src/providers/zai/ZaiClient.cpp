#include "providers/zai/ZaiClient.hpp"

#include "providers/zai/ZaiProvider.hpp"

#include <map>

namespace routerai {

namespace {

std::map<std::string, std::string> zaiHeaders(const std::string& apiKey) {
    return {
        {"Authorization", "Bearer " + apiKey},
        {"Accept-Language", "en-US,en"},
    };
}

}  // namespace

HttpResponse ZaiClient::chatCompletions(
    const std::string& apiKey,
    const std::string& jsonBody,
    long timeoutSeconds) {
    return HttpClient::postJson(
        ZaiProvider::generalBaseUrl() + "/chat/completions",
        jsonBody,
        zaiHeaders(apiKey),
        timeoutSeconds);
}

HttpResponse ZaiClient::chatCompletionsStream(
    const std::string& apiKey,
    const std::string& jsonBody,
    const HttpStreamCallback& onChunk,
    long timeoutSeconds) {
    auto headers = zaiHeaders(apiKey);
    headers.emplace("Accept", "text/event-stream");
    return HttpClient::postJsonStream(
        ZaiProvider::generalBaseUrl() + "/chat/completions",
        jsonBody,
        headers,
        onChunk,
        timeoutSeconds);
}

std::vector<std::string> ZaiClient::documentedChatModels() {
    return {
        "glm-5.1",
        "glm-5-turbo",
        "glm-5",
        "glm-4.7",
        "glm-4.7-flash",
        "glm-4.7-flashx",
        "glm-4.6",
        "glm-4.5",
        "glm-4.5-air",
        "glm-4.5-x",
        "glm-4.5-airx",
        "glm-4.5-flash",
        "glm-4-32b-0414-128k",
    };
}

std::vector<std::string> ZaiClient::documentedCodingPlanModels() {
    return {
        "glm-5.1",
        "glm-5-turbo",
        "glm-4.7",
        "glm-4.5-air",
    };
}

}  // namespace routerai
