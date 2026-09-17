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

HttpResponse ZaiClient::validateGeneralApiKey(
    const std::string& apiKey,
    long timeoutSeconds) {
    // Z.ai's public OpenAPI does not currently expose a model-list endpoint.
    // The tokenizer endpoint is documented, authenticated, and does not
    // generate a completion, so it is used as the light-weight credential
    // validation probe for General API credentials.
    const std::string body =
        R"({"model":"glm-4.6","messages":[{"role":"user","content":"routerAI credential check"}]})";

    return HttpClient::postJson(
        ZaiProvider::generalBaseUrl() + "/tokenizer",
        body,
        zaiHeaders(apiKey),
        timeoutSeconds);
}

std::vector<std::string> ZaiClient::documentedChatModels() {
    // Kept in the same order as the current Z.ai OpenAPI chat-completion enum.
    return {
        "glm-5.2",
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

}  // namespace routerai
