#include "core/CompletionRouter.hpp"

#include "api/OpenAICompat.hpp"
#include "providers/antigravity/AntigravityApiClient.hpp"
#include "providers/codex/CodexAppServerClient.hpp"
#include "providers/zai/ZaiClient.hpp"

#include <nlohmann/json.hpp>

#include <ctime>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace routerai {

namespace {

std::string messageContent(const nlohmann::json& message) {
    if (!message.contains("content") || message.at("content").is_null()) return {};
    const auto& content = message.at("content");
    if (content.is_string()) return content.get<std::string>();
    if (!content.is_array()) return {};

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

nlohmann::json openAiTextResponse(
    const std::string& text,
    const std::string& model,
    const std::string& sourceId,
    const std::string& finishReason = "stop",
    std::int64_t promptTokens = 0,
    std::int64_t completionTokens = 0,
    std::int64_t totalTokens = 0) {
    return {
        {"id", sourceId.empty()
            ? "chatcmpl-router-" + std::to_string(std::time(nullptr))
            : "chatcmpl-router-" + sourceId},
        {"object", "chat.completion"},
        {"created", static_cast<std::int64_t>(std::time(nullptr))},
        {"model", model.empty() ? "router" : model},
        {"choices", nlohmann::json::array({{{"index", 0}, {"message", {{"role", "assistant"}, {"content", text}}}, {"finish_reason", finishReason}}})},
        {"usage", {{"prompt_tokens", promptTokens}, {"completion_tokens", completionTokens}, {"total_tokens", totalTokens}}},
    };
}

nlohmann::json openAiResponse(
    const CodexCompletionResult& completion,
    const std::string& requestedModel) {
    const std::string model = completion.model.empty() ? requestedModel : completion.model;
    return openAiTextResponse(completion.text, model.empty() ? "codex" : model, completion.turnId);
}

CompletionRouteResult jsonError(long status, const std::string& message) {
    return CompletionRouteResult{
        status,
        nlohmann::json({{"error", {{"message", message}, {"type", "router_error"}}}}).dump(),
        "application/json", {}, {},
    };
}

bool retryableStatus(long status) {
    return status == 408 || status == 409 || status == 429 || status >= 500;
}

std::string interactionOutputText(const nlohmann::json& interaction) {
    const auto steps = interaction.find("steps");
    if (steps == interaction.end() || !steps->is_array()) return {};

    std::string latest;
    for (const auto& step : *steps) {
        if (!step.is_object() || step.value("type", std::string{}) != "model_output") continue;
        const auto content = step.find("content");
        if (content == step.end() || !content->is_array()) continue;

        std::string current;
        for (const auto& part : *content) {
            if (!part.is_object() || part.value("type", std::string{}) != "text") continue;
            const auto text = part.find("text");
            if (text != part.end() && text->is_string()) {
                if (!current.empty()) current += '\n';
                current += text->get<std::string>();
            }
        }
        if (!current.empty()) latest = std::move(current);
    }
    return latest;
}

std::int64_t usageValue(const nlohmann::json& interaction, const char* key) {
    const auto usage = interaction.find("usage");
    if (usage == interaction.end() || !usage->is_object()) return 0;
    const auto value = usage->find(key);
    if (value == usage->end() || (!value->is_number_integer() && !value->is_number_unsigned())) return 0;
    return value->get<std::int64_t>();
}

bool interactionHasUsableResult(const std::string& status) {
    return status == "completed" || status == "incomplete" || status == "budget_exceeded";
}

}  // namespace

CompletionRouter::CompletionRouter(
    SQLiteDatabase& database,
    CredentialStore& credentials,
    RoutingManager& routing)
    : database_(database), credentials_(credentials), routing_(routing) {}

CompletionRouteResult CompletionRouter::chatCompletions(
    const std::string& groupId,
    const std::string& requestBody) {
    nlohmann::json request;
    try {
        request = nlohmann::json::parse(requestBody);
    } catch (const nlohmann::json::parse_error& error) {
        return jsonError(400, "Invalid JSON: " + std::string(error.what()));
    }
    if (!request.is_object()) return jsonError(400, "Request body must be a JSON object");

    const auto group = routing_.findGroup(groupId);
    if (!group) return jsonError(404, "Routing group not found: " + groupId);
    if (!group->enabled) return jsonError(503, "Routing group is disabled: " + groupId);

    std::vector<std::string> attemptedAccountIds;
    attemptedAccountIds.reserve(group->accountIds.size());
    std::string lastError = "No eligible provider account";
    std::optional<std::string> requestConfigurationError;

    while (attemptedAccountIds.size() < group->accountIds.size()) {
        const auto decision = routing_.select(groupId, attemptedAccountIds);
        if (!decision) break;

        const Account account = decision->candidate.account;
        attemptedAccountIds.push_back(account.id);

        try {
            if (account.provider == "zai") {
                if (account.providerMode != "general-api") {
                    lastError = "Z.ai account is not configured for General API routing";
                    continue;
                }
                if (account.credentialRef.empty()) {
                    lastError = "Z.ai credential is not configured";
                    Account updated = account;
                    updated.status = AccountStatus::AuthExpired;
                    updated.lastError = lastError;
                    database_.updateAccount(updated);
                    continue;
                }
                const auto apiKey = credentials_.get(account.credentialRef);
                if (!apiKey || apiKey->empty()) {
                    lastError = "Z.ai credential is unavailable";
                    Account updated = account;
                    updated.status = AccountStatus::AuthExpired;
                    updated.lastError = lastError;
                    database_.updateAccount(updated);
                    continue;
                }

                const std::string selectedModel = openai_compat::resolveProviderModel(request, "zai");
                if (selectedModel.empty()) {
                    lastError = "Z.ai requires a provider model. Set model to a Z.ai model or provide router.models.zai.";
                    requestConfigurationError = lastError;
                    continue;
                }

                nlohmann::json outgoing = request;
                outgoing.erase("router");
                outgoing["model"] = selectedModel;
                outgoing["stream"] = false;

                const HttpResponse response = ZaiClient::chatCompletions(*apiKey, outgoing.dump());
                if (response.succeeded()) {
                    routing_.recordSuccess(account.id);
                    if (auto verified = database_.findAccount(account.id)) {
                        if (verified->status == AccountStatus::Warning) {
                            verified->status = AccountStatus::Ready;
                            verified->lastError.clear();
                            database_.updateAccount(*verified);
                        }
                    }
                    return CompletionRouteResult{
                        response.statusCode,
                        response.body,
                        response.contentType.empty() ? "application/json" : response.contentType,
                        account.id,
                        account.provider,
                    };
                }

                lastError = response.error.empty()
                    ? "Z.ai HTTP " + std::to_string(response.statusCode)
                    : response.error;

                if (response.statusCode == 401 || response.statusCode == 403) {
                    Account updated = account;
                    updated.status = AccountStatus::AuthExpired;
                    updated.lastError = lastError;
                    database_.updateAccount(updated);
                    continue;
                }

                if (response.error.empty() && !retryableStatus(response.statusCode)) {
                    return CompletionRouteResult{
                        response.statusCode,
                        response.body,
                        response.contentType.empty() ? "application/json" : response.contentType,
                        account.id,
                        account.provider,
                    };
                }

                routing_.recordFailure(account.id, lastError, static_cast<std::int64_t>(std::time(nullptr)));
                continue;
            }

            if (account.provider == "antigravity" && account.providerMode == "api-project") {
                if (account.credentialRef.empty()) {
                    lastError = "Antigravity Gemini API credential is not configured";
                    Account updated = account;
                    updated.status = AccountStatus::AuthExpired;
                    updated.lastError = lastError;
                    database_.updateAccount(updated);
                    continue;
                }
                const auto apiKey = credentials_.get(account.credentialRef);
                if (!apiKey || apiKey->empty()) {
                    lastError = "Antigravity Gemini API credential is unavailable";
                    Account updated = account;
                    updated.status = AccountStatus::AuthExpired;
                    updated.lastError = lastError;
                    database_.updateAccount(updated);
                    continue;
                }

                const CodexPrompt prompt = toCodexPrompt(request);
                nlohmann::json outgoing = {
                    {"agent", AntigravityApiClient::agentName()},
                    {"input", prompt.prompt},
                    {"environment", "remote"},
                    {"stream", false},
                };
                const std::string instructions = combinedInstructions(prompt);
                if (!instructions.empty()) outgoing["system_instruction"] = instructions;

                const std::string modelOverride = openai_compat::resolveProviderModelOverride(request, "antigravity");
                if (!modelOverride.empty()) {
                    outgoing["agent_config"] = {{"type", "antigravity"}, {"model", modelOverride}};
                }

                const HttpResponse response = AntigravityApiClient::createInteraction(*apiKey, outgoing.dump());
                if (!response.succeeded()) {
                    lastError = response.error.empty()
                        ? "Antigravity API HTTP " + std::to_string(response.statusCode)
                        : response.error;

                    if (response.statusCode == 401 || response.statusCode == 403) {
                        Account updated = account;
                        updated.status = AccountStatus::AuthExpired;
                        updated.lastError = lastError;
                        database_.updateAccount(updated);
                        continue;
                    }
                    if (response.error.empty() && !retryableStatus(response.statusCode)) {
                        return CompletionRouteResult{
                            response.statusCode,
                            response.body,
                            response.contentType.empty() ? "application/json" : response.contentType,
                            account.id,
                            account.provider,
                        };
                    }
                    routing_.recordFailure(account.id, lastError, static_cast<std::int64_t>(std::time(nullptr)));
                    continue;
                }

                const auto interaction = nlohmann::json::parse(response.body);
                const std::string interactionStatus = interaction.value("status", std::string{});
                if (!interactionHasUsableResult(interactionStatus)) {
                    lastError = "Antigravity interaction finished with status: " + interactionStatus;
                    routing_.recordFailure(account.id, lastError, static_cast<std::int64_t>(std::time(nullptr)));
                    continue;
                }

                const std::string text = interactionOutputText(interaction);
                if (text.empty()) {
                    lastError = "Antigravity interaction returned no text output";
                    if (interactionStatus == "completed") {
                        routing_.recordFailure(account.id, lastError, static_cast<std::int64_t>(std::time(nullptr)));
                        continue;
                    }
                    return jsonError(422, lastError);
                }

                routing_.recordSuccess(account.id);
                const std::string responseModel = interaction.value(
                    "model",
                    modelOverride.empty() ? AntigravityApiClient::agentName() : modelOverride);
                const std::string interactionId = interaction.value("id", std::string{});
                const std::string finishReason = interactionStatus == "completed" ? "stop" : "length";
                return CompletionRouteResult{
                    200,
                    openAiTextResponse(
                        text,
                        responseModel,
                        interactionId,
                        finishReason,
                        usageValue(interaction, "total_input_tokens"),
                        usageValue(interaction, "total_output_tokens"),
                        usageValue(interaction, "total_tokens")).dump(),
                    "application/json",
                    account.id,
                    account.provider,
                };
            }

            if (account.provider == "codex") {
                const CodexPrompt prompt = toCodexPrompt(request);
                const std::string model = openai_compat::resolveProviderModel(request, "codex");
                CodexAppServerClient client(account.runtimeHome);
                const CodexCompletionResult completion = client.runPrompt(
                    prompt.prompt,
                    model,
                    prompt.baseInstructions,
                    prompt.developerInstructions);
                routing_.recordSuccess(account.id);
                return CompletionRouteResult{
                    200,
                    openAiResponse(completion, model).dump(),
                    "application/json",
                    account.id,
                    account.provider,
                };
            }

            lastError = "Provider execution is not implemented: " + account.provider;
        } catch (const std::exception& exception) {
            lastError = exception.what();
            routing_.recordFailure(account.id, lastError, static_cast<std::int64_t>(std::time(nullptr)));
        }
    }

    if (requestConfigurationError) return jsonError(400, *requestConfigurationError);
    return jsonError(503, lastError);
}

}  // namespace routerai
