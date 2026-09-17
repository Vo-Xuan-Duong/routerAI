#include "providers/antigravity/AntigravityApiClient.hpp"

#include <map>

namespace routerai {

HttpResponse AntigravityApiClient::createInteraction(
    const std::string& apiKey,
    const std::string& jsonBody,
    long timeoutSeconds) {
    const std::map<std::string, std::string> headers = {
        {"x-goog-api-key", apiKey},
        {"Accept", "application/json"},
    };

    return HttpClient::postJson(
        endpoint(),
        jsonBody,
        headers,
        timeoutSeconds);
}

HttpResponse AntigravityApiClient::listModels(
    const std::string& apiKey,
    long timeoutSeconds) {
    const std::map<std::string, std::string> headers = {
        {"Authorization", "Bearer " + apiKey},
        {"Accept", "application/json"},
    };
    return HttpClient::get(modelsEndpoint(), headers, timeoutSeconds);
}

std::vector<std::string> AntigravityApiClient::supportedAgentModels() {
    // Models documented for agent_config.model on the current Antigravity
    // managed-agent documentation.
    return {
        "gemini-3.8-flash",
        "gemini-3.7-flash",
        "gemini-3.6-flash",
        "gemini-3.5-flash",
        "gemini-3.5-flash-lite",
    };
}

std::string AntigravityApiClient::endpoint() {
    return "https://generativelanguage.googleapis.com/v1beta/interactions";
}

std::string AntigravityApiClient::modelsEndpoint() {
    return "https://generativelanguage.googleapis.com/v1beta/openai/models";
}

std::string AntigravityApiClient::agentName() {
    return "antigravity-preview-05-2026";
}

}  // namespace routerai
