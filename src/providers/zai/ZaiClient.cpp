#include "providers/zai/ZaiClient.hpp"

#include "providers/zai/ZaiProvider.hpp"

#include <map>

namespace routerai {

HttpResponse ZaiClient::chatCompletions(
    const std::string& apiKey,
    const std::string& jsonBody,
    long timeoutSeconds) {
    const std::map<std::string, std::string> headers = {
        {"Authorization", "Bearer " + apiKey},
        {"Accept-Language", "en-US,en"},
    };

    return HttpClient::postJson(
        ZaiProvider::generalBaseUrl() + "/chat/completions",
        jsonBody,
        headers,
        timeoutSeconds);
}

}  // namespace routerai
