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

std::string AntigravityApiClient::endpoint() {
    return "https://generativelanguage.googleapis.com/v1beta/interactions";
}

std::string AntigravityApiClient::agentName() {
    return "antigravity-preview-05-2026";
}

}  // namespace routerai
