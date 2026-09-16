#include "api/OpenAICompat.hpp"

#include <nlohmann/json.hpp>

#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

}  // namespace

int main() {
    try {
        using nlohmann::json;
        using namespace routerai::openai_compat;

        const json routed = {
            {"model", "router/mixed-default"},
            {"stream", true},
            {"router",
             {
                 {"group", "zai-default"},
                 {"models", {{"zai", "glm-5.2"}, {"codex", "gpt-5"}, {"antigravity", "gemini-3.5-flash"}}},
             }},
        };

        require(wantsStreaming(routed), "stream=true must be detected");
        require(
            resolveRoutingGroup(routed, "header-group") == "header-group",
            "X-Router-Group must take precedence");
        require(
            resolveRoutingGroup(routed, "") == "zai-default",
            "router.group must be used when there is no header group");
        require(
            resolveProviderModelOverride(routed, "antigravity") == "gemini-3.5-flash",
            "explicit Antigravity model override mismatch");
        require(
            resolveProviderModel(routed, "zai") == "glm-5.2",
            "provider model override must win over router pseudo-model");
        require(
            resolveProviderModel(routed, "codex") == "gpt-5",
            "provider-specific model override mismatch");
        require(
            resolveProviderModel(routed, "unknown").empty(),
            "router/<group> must never be forwarded as a provider model");

        const json modelSelected = {{"model", "router/codex-default"}};
        require(
            resolveRoutingGroup(modelSelected, "") == "codex-default",
            "router/<group> model must select its routing group");
        require(
            resolveProviderModel(modelSelected, "codex").empty(),
            "routing pseudo-model must resolve to no provider model");
        require(
            resolveProviderModelOverride(modelSelected, "codex").empty(),
            "a routing pseudo-model must not appear as an explicit provider override");

        const json directProviderModel = {{"model", "glm-5.2"}};
        require(
            resolveProviderModel(directProviderModel, "zai") == "glm-5.2",
            "a real model name must pass through to the provider");
        require(
            resolveProviderModelOverride(directProviderModel, "zai").empty(),
            "a direct model name is not a provider-specific override");
        require(
            resolveRoutingGroup(directProviderModel, "") == "mixed-default",
            "normal model names must use the fallback routing group");

        const json completion = {
            {"id", "chatcmpl-test"},
            {"object", "chat.completion"},
            {"created", 123},
            {"model", "glm-5.2"},
            {"choices",
             json::array({
                 {
                     {"index", 0},
                     {"message", {{"role", "assistant"}, {"content", "hello"}}},
                     {"finish_reason", "stop"},
                 },
             })},
        };

        const std::string sse = bufferedChatCompletionSse(completion);
        require(sse.find("chat.completion.chunk") != std::string::npos, "SSE chunk object missing");
        require(sse.find("hello") != std::string::npos, "SSE content missing");
        require(sse.ends_with("data: [DONE]\n\n"), "SSE stream must end with [DONE]");

        bool invalidRejected = false;
        try {
            bufferedChatCompletionSse(json::object());
        } catch (const std::exception&) {
            invalidRejected = true;
        }
        require(invalidRejected, "invalid completion responses must be rejected for SSE conversion");

        std::cout << "OpenAICompatTests: OK\n";
    } catch (const std::exception& exception) {
        std::cerr << "OpenAICompatTests: FAILED: " << exception.what() << '\n';
        return 1;
    }

    return 0;
}
