#include "core/CompletionRouter.hpp"

#include "api/OpenAICompat.hpp"
#include "providers/codex/CodexAppServerClient.hpp"
#include "providers/zai/ZaiClient.hpp"

#include <nlohmann/json.hpp>

#include <ctime>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace routerai {

namespace {

std::string messageContent(const nlohmann::json& message) {
    if (!message.contains("content") || message.at("content").is_null()) {
        return {};
    }
    const auto& content = message.at("content");
    if (content.is_string()) {
        return content.get<std::string>();
    }
    if (!content.is_array()) {
        return {};
    }

    std::string text;
    for (const auto& part : content) {
        if (!part.is_object()) {
            continue;
        }
        const std::string type = part.value("type", std::string{});
        if ((type == "text" || type == "input_text") &&
            part.contains("text") && part.at("text").is_string()) {
            if (!text.empty()) {
                text += '\n';
            }
            text += part.at("text").get<std::string>();
        }
    }
    return text;
}

struct CodexPrompt {
    std::string prompt;
    std::string baseInstructions;
    std::string developerInstructions;
};

CodexPrompt toCodexPrompt(const nlohmann::json& request) {
    if (!request.contains("messages") || !request.at("messages").is_array()) {
        throw std::runtime_error("chat/completions request must contain a messages array");
    }

    CodexPrompt output;
    for (const auto& message : request.at("messages")) {
        if (!message.is_object()) {
            continue;
        }
        const std::string role = message.value("role", std::string{});
        const std::string content = messageContent(message);
        if (content.empty()) {
            continue;
        }
        if (role == "system") {
            if (!output.baseInstructions.empty()) output.baseInstructions += "\n\n";
            output.baseInstructions += content;
            continue;
        }
        if (role == "developer") {
            if (!output.developerInstructions.empty()) output.developerInstructions += "\n\n";
            output.developerInstructions += content;
            continue;
        }
        if (!output.prompt.empty()) output.prompt += "\n\n";
        output.prompt += role.empty() ? "user" : role;
        output.prompt += ":\n";
        output.prompt += content;
    }

    if (output.prompt.empty()) {
        throw std::runtime_error("No text user/assistant messages were provided");
    }
    return output;
}

CompletionStreamResult streamError(long status, const std::string& message) {
    return CompletionStreamResult{
        status,
        nlohmann::json({
            {"error", {{"message", message}, {"type", "router_stream_error"}}},
        }).dump(),
        "application/json",
        {},
        {},
        "none",
        false,
    };
}

bool retryableStatus(long status) {
    return status == 408 || status == 409 || status == 429 || status >= 500;
}

std::string codexSseChunk(
    const std::string& id,
    const std::string& model,
    const std::string& delta,
    bool includeRole,
    const std::optional<std::string>& finishReason = std::nullopt) {
    nlohmann::json deltaObject = nlohmann::json::object();
    if (includeRole) {
        deltaObject["role"] = "assistant";
    }
    if (!delta.empty()) {
        deltaObject["content"] = delta;
    }

    nlohmann::json choice = {
        {"index", 0},
        {"delta", std::move(deltaObject)},
        {"finish_reason", finishReason ? nlohmann::json(*finishReason) : nlohmann::json(nullptr)},
    };

    const nlohmann::json chunk = {
        {"id", id},
        {"object", "chat.completion.chunk"},
        {"created", static_cast<std::int64_t>(std::time(nullptr))},
        {"model", model.empty() ? "codex" : model},
        {"choices", nlohmann::json::array({std::move(choice)})},
    };
    return "data: " + chunk.dump() + "\n\n";
}

bool groupIsNativeProvider(
    SQLiteDatabase& database,
    const RoutingGroup& group,
    const std::string& provider) {
    bool found = false;
    for (const auto& accountId : group.accountIds) {
        const auto account = database.findAccount(accountId);
        if (!account) {
            continue;
        }
        found = true;
        if (account->provider != provider) {
            return false;
        }
        if (provider == "zai" && account->providerMode != "general-api") {
            return false;
        }
    }
    return found;
}

}  // namespace

CompletionStreamResult CompletionRouter::streamChatCompletions(
    const std::string& groupId,
    const std::string& requestBody,
    const CompletionStreamCallback& onChunk) {
    nlohmann::json request;
    try {
        request = nlohmann::json::parse(requestBody);
    } catch (const nlohmann::json::parse_error& error) {
        return streamError(400, "Invalid JSON: " + std::string(error.what()));
    }
    if (!request.is_object()) {
        return streamError(400, "Request body must be a JSON object");
    }

    const auto group = routing_.findGroup(groupId);
    if (!group) {
        return streamError(404, "Routing group not found: " + groupId);
    }
    if (!group->enabled) {
        return streamError(503, "Routing group is disabled: " + groupId);
    }

    const bool nativeZai = groupIsNativeProvider(database_, *group, "zai");
    const bool nativeCodex = groupIsNativeProvider(database_, *group, "codex");

    // Mixed groups and Antigravity currently retain the buffered compatibility
    // path. This preserves existing cross-provider failover while native
    // streaming is enabled incrementally per provider.
    if (!nativeZai && !nativeCodex) {
        const CompletionRouteResult buffered = chatCompletions(groupId, requestBody);
        if (buffered.statusCode < 200 || buffered.statusCode >= 300) {
            return CompletionStreamResult{
                buffered.statusCode,
                buffered.body,
                buffered.contentType,
                buffered.accountId,
                buffered.provider,
                "buffered",
                false,
            };
        }
        try {
            const std::string sse = openai_compat::bufferedChatCompletionSse(
                nlohmann::json::parse(buffered.body));
            if (!onChunk(sse)) {
                return streamError(499, "Stream consumer cancelled");
            }
            return CompletionStreamResult{
                200,
                {},
                "text/event-stream",
                buffered.accountId,
                buffered.provider,
                "buffered",
                true,
            };
        } catch (const std::exception& exception) {
            return streamError(502, exception.what());
        }
    }

    std::vector<std::string> attemptedAccountIds;
    attemptedAccountIds.reserve(group->accountIds.size());
    std::string lastError = "No eligible provider account";

    while (attemptedAccountIds.size() < group->accountIds.size()) {
        const auto decision = routing_.select(groupId, attemptedAccountIds);
        if (!decision) {
            break;
        }
        const Account account = decision->candidate.account;
        attemptedAccountIds.push_back(account.id);

        if (nativeZai) {
            if (account.credentialRef.empty()) {
                lastError = "Z.ai credential is not configured";
                continue;
            }
            const auto apiKey = credentials_.get(account.credentialRef);
            if (!apiKey || apiKey->empty()) {
                lastError = "Z.ai credential is unavailable";
                continue;
            }

            const std::string selectedModel =
                openai_compat::resolveProviderModel(request, "zai");
            if (selectedModel.empty()) {
                return streamError(
                    400,
                    "Z.ai requires a provider model. Set model to a Z.ai model or provide router.models.zai.");
            }

            nlohmann::json outgoing = request;
            outgoing.erase("router");
            outgoing["model"] = selectedModel;
            outgoing["stream"] = true;

            bool emitted = false;
            const HttpResponse response = ZaiClient::chatCompletionsStream(
                *apiKey,
                outgoing.dump(),
                [&](std::string_view chunk) {
                    emitted = true;
                    return onChunk(chunk);
                });

            if (response.succeeded()) {
                routing_.recordSuccess(account.id);
                return CompletionStreamResult{
                    response.statusCode,
                    {},
                    "text/event-stream",
                    account.id,
                    account.provider,
                    "native",
                    emitted,
                };
            }

            if (response.error == "stream callback cancelled") {
                return CompletionStreamResult{
                    499,
                    response.error,
                    "application/json",
                    account.id,
                    account.provider,
                    "native",
                    emitted,
                };
            }

            lastError = response.error.empty()
                ? "Z.ai HTTP " + std::to_string(response.statusCode)
                : response.error;

            if (emitted) {
                routing_.recordFailure(
                    account.id,
                    lastError,
                    static_cast<std::int64_t>(std::time(nullptr)));
                return CompletionStreamResult{
                    502,
                    lastError,
                    "application/json",
                    account.id,
                    account.provider,
                    "native",
                    true,
                };
            }

            if (response.statusCode == 401 || response.statusCode == 403) {
                Account updated = account;
                updated.status = AccountStatus::AuthExpired;
                updated.lastError = lastError;
                database_.updateAccount(updated);
                continue;
            }
            if (response.error.empty() && !retryableStatus(response.statusCode)) {
                return CompletionStreamResult{
                    response.statusCode,
                    response.body,
                    response.contentType.empty() ? "application/json" : response.contentType,
                    account.id,
                    account.provider,
                    "native",
                    false,
                };
            }
            routing_.recordFailure(
                account.id,
                lastError,
                static_cast<std::int64_t>(std::time(nullptr)));
            continue;
        }

        if (nativeCodex) {
            try {
                const CodexPrompt prompt = toCodexPrompt(request);
                const std::string model =
                    openai_compat::resolveProviderModel(request, "codex");
                const std::string streamId =
                    "chatcmpl-router-codex-" + std::to_string(std::time(nullptr));
                bool emitted = false;
                bool firstDelta = true;
                CodexAppServerClient client(account.runtimeHome);
                const CodexCompletionResult completion = client.runPromptStreaming(
                    prompt.prompt,
                    [&](std::string_view delta) {
                        const std::string chunk = codexSseChunk(
                            streamId,
                            model,
                            std::string(delta),
                            firstDelta);
                        firstDelta = false;
                        emitted = true;
                        return onChunk(chunk);
                    },
                    model,
                    prompt.baseInstructions,
                    prompt.developerInstructions);

                const std::string responseModel = completion.model.empty()
                    ? model
                    : completion.model;
                if (!onChunk(codexSseChunk(
                        streamId,
                        responseModel,
                        {},
                        firstDelta,
                        std::string("stop")))) {
                    return CompletionStreamResult{
                        499,
                        "Stream consumer cancelled",
                        "application/json",
                        account.id,
                        account.provider,
                        "native",
                        emitted,
                    };
                }
                if (!onChunk("data: [DONE]\n\n")) {
                    return CompletionStreamResult{
                        499,
                        "Stream consumer cancelled",
                        "application/json",
                        account.id,
                        account.provider,
                        "native",
                        true,
                    };
                }
                routing_.recordSuccess(account.id);
                return CompletionStreamResult{
                    200,
                    {},
                    "text/event-stream",
                    account.id,
                    account.provider,
                    "native",
                    true,
                };
            } catch (const std::exception& exception) {
                lastError = exception.what();
                if (lastError == "Codex stream consumer cancelled") {
                    return CompletionStreamResult{
                        499,
                        lastError,
                        "application/json",
                        account.id,
                        account.provider,
                        "native",
                        true,
                    };
                }
                routing_.recordFailure(
                    account.id,
                    lastError,
                    static_cast<std::int64_t>(std::time(nullptr)));
                continue;
            }
        }
    }

    return streamError(503, lastError);
}

}  // namespace routerai
