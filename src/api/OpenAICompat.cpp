#include "api/OpenAICompat.hpp"

#include <cstdint>
#include <stdexcept>

namespace routerai::openai_compat {

namespace {

bool isRouterPseudoModel(const std::string& model) {
    return model.starts_with("router/");
}

}  // namespace

bool wantsStreaming(const nlohmann::json& request) {
    return request.is_object() && request.value("stream", false);
}

std::string resolveRoutingGroup(
    const nlohmann::json& request,
    const std::string& headerGroup,
    const std::string& fallbackGroup) {
    if (!headerGroup.empty()) {
        return headerGroup;
    }

    if (request.is_object()) {
        const auto router = request.find("router");
        if (router != request.end() && router->is_object()) {
            const std::string group = router->value("group", std::string{});
            if (!group.empty()) {
                return group;
            }
        }

        const std::string model = request.value("model", std::string{});
        constexpr const char* prefix = "router/";
        if (model.starts_with(prefix)) {
            const std::string group = model.substr(std::char_traits<char>::length(prefix));
            if (!group.empty()) {
                return group;
            }
        }
    }

    return fallbackGroup;
}

std::string resolveProviderModel(
    const nlohmann::json& request,
    const std::string& provider) {
    if (!request.is_object()) {
        return {};
    }

    const auto router = request.find("router");
    if (router != request.end() && router->is_object()) {
        const auto models = router->find("models");
        if (models != router->end() && models->is_object()) {
            const auto it = models->find(provider);
            if (it != models->end() && it->is_string()) {
                return it->get<std::string>();
            }
        }
    }

    const std::string model = request.value("model", std::string{});
    return isRouterPseudoModel(model) ? std::string{} : model;
}

std::string bufferedChatCompletionSse(const nlohmann::json& full) {
    if (!full.is_object() ||
        !full.contains("choices") ||
        !full.at("choices").is_array() ||
        full.at("choices").empty()) {
        throw std::runtime_error(
            "Provider response cannot be converted to chat completion SSE");
    }

    const auto& choice = full.at("choices").front();
    std::string content;
    std::string finishReason = "stop";
    if (choice.is_object()) {
        const auto message = choice.find("message");
        if (message != choice.end() && message->is_object()) {
            const auto value = message->find("content");
            if (value != message->end() && value->is_string()) {
                content = value->get<std::string>();
            }
        }
        const auto finish = choice.find("finish_reason");
        if (finish != choice.end() && finish->is_string()) {
            finishReason = finish->get<std::string>();
        }
    }

    const std::string id = full.value(
        "id",
        std::string("chatcmpl-router-buffered"));
    const std::string model = full.value("model", std::string("router"));
    const std::int64_t created = full.value(
        "created",
        static_cast<std::int64_t>(0));

    const nlohmann::json contentChunk = {
        {"id", id},
        {"object", "chat.completion.chunk"},
        {"created", created},
        {"model", model},
        {"choices",
         nlohmann::json::array({
             {
                 {"index", 0},
                 {"delta", {{"role", "assistant"}, {"content", content}}},
                 {"finish_reason", nullptr},
             },
         })},
    };

    const nlohmann::json finishChunk = {
        {"id", id},
        {"object", "chat.completion.chunk"},
        {"created", created},
        {"model", model},
        {"choices",
         nlohmann::json::array({
             {
                 {"index", 0},
                 {"delta", nlohmann::json::object()},
                 {"finish_reason", finishReason},
             },
         })},
    };

    return "data: " + contentChunk.dump() + "\n\n" +
           "data: " + finishChunk.dump() + "\n\n" +
           "data: [DONE]\n\n";
}

}  // namespace routerai::openai_compat
