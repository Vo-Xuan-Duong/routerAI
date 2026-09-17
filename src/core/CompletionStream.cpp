#include "core/CompletionRouter.hpp"

#include "api/OpenAICompat.hpp"
#include "providers/antigravity/AntigravityApiClient.hpp"
#include "providers/codex/CodexAppServerClient.hpp"
#include "providers/zai/ZaiClient.hpp"

#include <nlohmann/json.hpp>

#include <ctime>
#include <optional>
#include <sstream>
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
        if (!part.is_object()) continue;
        const std::string type = part.value("type", std::string{});
        if ((type == "text" || type == "input_text") &&
            part.contains("text") && part.at("text").is_string()) {
            if (!text.empty()) text += '\n';
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
        if (!message.is_object()) continue;
        const std::string role = message.value("role", std::string{});
        const std::string content = messageContent(message);
        if (content.empty()) continue;
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

std::string combinedInstructions(const CodexPrompt& prompt) {
    if (prompt.baseInstructions.empty()) return prompt.developerInstructions;
    if (prompt.developerInstructions.empty()) return prompt.baseInstructions;
    return prompt.baseInstructions + "\n\n" + prompt.developerInstructions;
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

std::string openAiSseChunk(
    const std::string& id,
    const std::string& model,
    const std::string& delta,
    bool includeRole,
    const std::optional<std::string>& finishReason = std::nullopt) {
    nlohmann::json deltaObject = nlohmann::json::object();
    if (includeRole) deltaObject["role"] = "assistant";
    if (!delta.empty()) deltaObject["content"] = delta;

    nlohmann::json choice = {
        {"index", 0},
        {"delta", std::move(deltaObject)},
        {"finish_reason", finishReason ? nlohmann::json(*finishReason) : nlohmann::json(nullptr)},
    };

    const nlohmann::json chunk = {
        {"id", id},
        {"object", "chat.completion.chunk"},
        {"created", static_cast<std::int64_t>(std::time(nullptr))},
        {"model", model.empty() ? "router" : model},
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
        if (!account) continue;
        found = true;
        if (account->provider != provider) return false;
        if (provider == "zai" && account->providerMode != "general-api") return false;
        if (provider == "antigravity" && account->providerMode != "api-project") return false;
    }
    return found;
}

struct AntigravitySseState {
    std::string buffer;
    std::string id{"chatcmpl-router-antigravity-" + std::to_string(std::time(nullptr))};
    std::string model{AntigravityApiClient::agentName()};
    bool firstDelta{true};
    bool emitted{false};
    bool done{false};
    bool providerError{false};
    std::string error;
};

std::pair<std::string, std::string> parseSseBlock(const std::string& block) {
    std::istringstream input(block);
    std::string line;
    std::string event;
    std::string data;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.starts_with("event:")) {
            event = line.substr(6);
            while (!event.empty() && event.front() == ' ') event.erase(event.begin());
        } else if (line.starts_with("data:")) {
            std::string part = line.substr(5);
            while (!part.empty() && part.front() == ' ') part.erase(part.begin());
            if (!data.empty()) data += '\n';
            data += part;
        }
    }
    return {event, data};
}

bool emitAntigravityEvent(
    AntigravitySseState& state,
    const std::string& block,
    const CompletionStreamCallback& onChunk) {
    const auto [event, data] = parseSseBlock(block);
    if (data.empty()) return true;

    if (data == "[DONE]") {
        if (!state.done) {
            if (!onChunk(openAiSseChunk(
                    state.id,
                    state.model,
                    {},
                    state.firstDelta,
                    std::string("stop")))) return false;
            state.emitted = true;
            if (!onChunk("data: [DONE]\n\n")) return false;
            state.done = true;
        }
        return true;
    }

    nlohmann::json payload;
    try {
        payload = nlohmann::json::parse(data);
    } catch (...) {
        return true;
    }

    const std::string eventType = payload.value(
        "event_type",
        event.empty() ? std::string{} : event);

    if (eventType == "interaction.created") {
        const auto interaction = payload.find("interaction");
        if (interaction != payload.end() && interaction->is_object()) {
            const std::string id = interaction->value("id", std::string{});
            const std::string model = interaction->value("model", std::string{});
            if (!id.empty()) state.id = "chatcmpl-router-" + id;
            if (!model.empty()) state.model = model;
        }
        return true;
    }

    if (eventType == "step.delta") {
        const auto delta = payload.find("delta");
        if (delta == payload.end() || !delta->is_object()) return true;
        if (delta->value("type", std::string{}) != "text") return true;
        const std::string text = delta->value("text", std::string{});
        if (text.empty()) return true;
        if (!onChunk(openAiSseChunk(
                state.id,
                state.model,
                text,
                state.firstDelta))) return false;
        state.firstDelta = false;
        state.emitted = true;
        return true;
    }

    if (eventType == "interaction.completed") {
        std::string finishReason = "stop";
        const auto interaction = payload.find("interaction");
        if (interaction != payload.end() && interaction->is_object()) {
            const std::string id = interaction->value("id", std::string{});
            const std::string model = interaction->value("model", std::string{});
            const std::string status = interaction->value("status", std::string("completed"));
            if (!id.empty()) state.id = "chatcmpl-router-" + id;
            if (!model.empty()) state.model = model;
            if (status != "completed") finishReason = "length";
        }
        if (!onChunk(openAiSseChunk(
                state.id,
                state.model,
                {},
                state.firstDelta,
                finishReason))) return false;
        state.emitted = true;
        if (!onChunk("data: [DONE]\n\n")) return false;
        state.done = true;
        return true;
    }

    if (eventType == "error") {
        state.providerError = true;
        const auto error = payload.find("error");
        state.error = error != payload.end() && error->is_object()
            ? error->value("message", std::string("Antigravity stream failed"))
            : std::string("Antigravity stream failed");
        const nlohmann::json errorPayload = {
            {"error", {{"message", state.error}, {"type", "provider_stream_error"}}},
        };
        const std::string errorEvent = "event: error\ndata: " + errorPayload.dump() + "\n\n";
        if (!onChunk(errorEvent)) return false;
        state.emitted = true;
        if (!onChunk("data: [DONE]\n\n")) return false;
        state.done = true;
        return true;
    }

    return true;
}

bool feedAntigravitySse(
    AntigravitySseState& state,
    std::string_view chunk,
    const CompletionStreamCallback& onChunk) {
    state.buffer.append(chunk.data(), chunk.size());

    while (true) {
        const std::size_t lf = state.buffer.find("\n\n");
        const std::size_t crlf = state.buffer.find("\r\n\r\n");
        std::size_t position = std::string::npos;
        std::size_t separatorLength = 0;
        if (lf != std::string::npos && (crlf == std::string::npos || lf < crlf)) {
            position = lf;
            separatorLength = 2;
        } else if (crlf != std::string::npos) {
            position = crlf;
            separatorLength = 4;
        }
        if (position == std::string::npos) break;

        const std::string block = state.buffer.substr(0, position);
        state.buffer.erase(0, position + separatorLength);
        if (!emitAntigravityEvent(state, block, onChunk)) return false;
    }
    return true;
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
    if (!request.is_object()) return streamError(400, "Request body must be a JSON object");

    const auto group = routing_.findGroup(groupId);
    if (!group) return streamError(404, "Routing group not found: " + groupId);
    if (!group->enabled) return streamError(503, "Routing group is disabled: " + groupId);

    const bool nativeZai = groupIsNativeProvider(database_, *group, "zai");
    const bool nativeCodex = groupIsNativeProvider(database_, *group, "codex");
    const bool nativeAntigravity = groupIsNativeProvider(database_, *group, "antigravity");

    // Cross-provider groups retain buffered failover because once a native
    // stream has emitted bytes it cannot safely jump to another provider.
    if (!nativeZai && !nativeCodex && !nativeAntigravity) {
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
            if (!onChunk(sse)) return streamError(499, "Stream consumer cancelled");
            return CompletionStreamResult{
                200, {}, "text/event-stream", buffered.accountId, buffered.provider, "buffered", true};
        } catch (const std::exception& exception) {
            return streamError(502, exception.what());
        }
    }

    std::vector<std::string> attemptedAccountIds;
    attemptedAccountIds.reserve(group->accountIds.size());
    std::string lastError = "No eligible provider account";

    while (attemptedAccountIds.size() < group->accountIds.size()) {
        const auto decision = routing_.select(groupId, attemptedAccountIds);
        if (!decision) break;
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

            const std::string selectedModel = openai_compat::resolveProviderModel(request, "zai");
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
                    response.statusCode, {}, "text/event-stream", account.id, account.provider, "native", emitted};
            }
            if (response.error == "stream callback cancelled") {
                return CompletionStreamResult{
                    499, response.error, "application/json", account.id, account.provider, "native", emitted};
            }

            lastError = response.error.empty()
                ? "Z.ai HTTP " + std::to_string(response.statusCode)
                : response.error;
            if (emitted) {
                routing_.recordFailure(account.id, lastError, static_cast<std::int64_t>(std::time(nullptr)));
                return CompletionStreamResult{
                    502, lastError, "application/json", account.id, account.provider, "native", true};
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
                    false};
            }
            routing_.recordFailure(account.id, lastError, static_cast<std::int64_t>(std::time(nullptr)));
            continue;
        }

        if (nativeAntigravity) {
            if (account.credentialRef.empty()) {
                lastError = "Antigravity Gemini API credential is not configured";
                continue;
            }
            const auto apiKey = credentials_.get(account.credentialRef);
            if (!apiKey || apiKey->empty()) {
                lastError = "Antigravity Gemini API credential is unavailable";
                continue;
            }

            try {
                const CodexPrompt prompt = toCodexPrompt(request);
                nlohmann::json outgoing = {
                    {"agent", AntigravityApiClient::agentName()},
                    {"input", prompt.prompt},
                    {"environment", "remote"},
                    {"stream", true},
                };
                const std::string instructions = combinedInstructions(prompt);
                if (!instructions.empty()) outgoing["system_instruction"] = instructions;
                const std::string modelOverride =
                    openai_compat::resolveProviderModelOverride(request, "antigravity");
                if (!modelOverride.empty()) {
                    outgoing["agent_config"] = {
                        {"type", "antigravity"},
                        {"model", modelOverride},
                    };
                }

                AntigravitySseState state;
                if (!modelOverride.empty()) state.model = modelOverride;
                const HttpResponse response = AntigravityApiClient::createInteractionStream(
                    *apiKey,
                    outgoing.dump(),
                    [&](std::string_view chunk) {
                        return feedAntigravitySse(state, chunk, onChunk);
                    });

                if (response.succeeded() && !state.providerError) {
                    if (!state.done && !state.buffer.empty()) {
                        if (!emitAntigravityEvent(state, state.buffer, onChunk)) {
                            return CompletionStreamResult{
                                499, "Stream consumer cancelled", "application/json",
                                account.id, account.provider, "native", state.emitted};
                        }
                    }
                    if (!state.done) {
                        if (!onChunk(openAiSseChunk(
                                state.id, state.model, {}, state.firstDelta, std::string("stop"))) ||
                            !onChunk("data: [DONE]\n\n")) {
                            return CompletionStreamResult{
                                499, "Stream consumer cancelled", "application/json",
                                account.id, account.provider, "native", state.emitted};
                        }
                        state.emitted = true;
                    }
                    routing_.recordSuccess(account.id);
                    return CompletionStreamResult{
                        200, {}, "text/event-stream", account.id, account.provider, "native", state.emitted};
                }

                if (response.error == "stream callback cancelled") {
                    return CompletionStreamResult{
                        499, response.error, "application/json", account.id, account.provider, "native", state.emitted};
                }

                lastError = state.providerError
                    ? state.error
                    : (response.error.empty()
                        ? "Antigravity API HTTP " + std::to_string(response.statusCode)
                        : response.error);
                if (state.emitted) {
                    routing_.recordFailure(account.id, lastError, static_cast<std::int64_t>(std::time(nullptr)));
                    return CompletionStreamResult{
                        502, lastError, "application/json", account.id, account.provider, "native", true};
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
                        false};
                }
                routing_.recordFailure(account.id, lastError, static_cast<std::int64_t>(std::time(nullptr)));
                continue;
            } catch (const std::exception& exception) {
                lastError = exception.what();
                routing_.recordFailure(account.id, lastError, static_cast<std::int64_t>(std::time(nullptr)));
                continue;
            }
        }

        if (nativeCodex) {
            try {
                const CodexPrompt prompt = toCodexPrompt(request);
                const std::string model = openai_compat::resolveProviderModel(request, "codex");
                const std::string streamId =
                    "chatcmpl-router-codex-" + std::to_string(std::time(nullptr));
                bool emitted = false;
                bool firstDelta = true;
                CodexAppServerClient client(account.runtimeHome);
                const CodexCompletionResult completion = client.runPromptStreaming(
                    prompt.prompt,
                    [&](std::string_view delta) {
                        const std::string chunk = openAiSseChunk(
                            streamId, model, std::string(delta), firstDelta);
                        firstDelta = false;
                        emitted = true;
                        return onChunk(chunk);
                    },
                    model,
                    prompt.baseInstructions,
                    prompt.developerInstructions);

                const std::string responseModel = completion.model.empty() ? model : completion.model;
                if (!onChunk(openAiSseChunk(
                        streamId, responseModel, {}, firstDelta, std::string("stop")))) {
                    return CompletionStreamResult{
                        499, "Stream consumer cancelled", "application/json",
                        account.id, account.provider, "native", emitted};
                }
                if (!onChunk("data: [DONE]\n\n")) {
                    return CompletionStreamResult{
                        499, "Stream consumer cancelled", "application/json",
                        account.id, account.provider, "native", true};
                }
                routing_.recordSuccess(account.id);
                return CompletionStreamResult{
                    200, {}, "text/event-stream", account.id, account.provider, "native", true};
            } catch (const std::exception& exception) {
                lastError = exception.what();
                if (lastError == "Codex stream consumer cancelled") {
                    return CompletionStreamResult{
                        499, lastError, "application/json",
                        account.id, account.provider, "native", true};
                }
                routing_.recordFailure(account.id, lastError, static_cast<std::int64_t>(std::time(nullptr)));
                continue;
            }
        }
    }

    return streamError(503, lastError);
}

}  // namespace routerai
