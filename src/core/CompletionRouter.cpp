#include "core/CompletionRouter.hpp"

#include "providers/codex/CodexAppServerClient.hpp"
#include "providers/zai/ZaiClient.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <ctime>
#include <optional>
#include <stdexcept>
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
            if (!output.baseInstructions.empty()) {
                output.baseInstructions += "\n\n";
            }
            output.baseInstructions += content;
            continue;
        }
        if (role == "developer") {
            if (!output.developerInstructions.empty()) {
                output.developerInstructions += "\n\n";
            }
            output.developerInstructions += content;
            continue;
        }

        if (!output.prompt.empty()) {
            output.prompt += "\n\n";
        }
        output.prompt += role.empty() ? "user" : role;
        output.prompt += ":\n";
        output.prompt += content;
    }

    if (output.prompt.empty()) {
        throw std::runtime_error("No text user/assistant messages were provided");
    }
    return output;
}

nlohmann::json openAiResponse(
    const CodexCompletionResult& completion,
    const std::string& requestedModel) {
    const std::string model = completion.model.empty()
        ? requestedModel
        : completion.model;
    return {
        {"id", "chatcmpl-router-" + std::to_string(std::time(nullptr))},
        {"object", "chat.completion"},
        {"created", static_cast<std::int64_t>(std::time(nullptr))},
        {"model", model.empty() ? "codex" : model},
        {"choices",
         nlohmann::json::array({
             {
                 {"index", 0},
                 {"message", {{"role", "assistant"}, {"content", completion.text}}},
                 {"finish_reason", "stop"},
             },
         })},
        {"usage", {{"prompt_tokens", 0}, {"completion_tokens", 0}, {"total_tokens", 0}}},
    };
}

CompletionRouteResult jsonError(long status, const std::string& message) {
    return CompletionRouteResult{
        status,
        nlohmann::json({
            {"error", {{"message", message}, {"type", "router_error"}}},
        }).dump(),
        "application/json",
        {},
        {},
    };
}

bool retryableStatus(long status) {
    return status == 408 || status == 409 || status == 429 || status >= 500;
}

bool isRouterPseudoModel(const std::string& model) {
    return model.starts_with("router/");
}

std::string providerModel(
    const nlohmann::json& request,
    const std::string& provider) {
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

    if (!request.is_object()) {
        return jsonError(400, "Request body must be a JSON object");
    }

    const auto group = routing_.findGroup(groupId);
    if (!group) {
        return jsonError(404, "Routing group not found: " + groupId);
    }
    if (!group->enabled) {
        return jsonError(503, "Routing group is disabled: " + groupId);
    }

    std::vector<std::string> attemptedAccountIds;
    attemptedAccountIds.reserve(group->accountIds.size());
    std::string lastError = "No eligible provider account";
    std::optional<std::string> requestConfigurationError;

    while (attemptedAccountIds.size() < group->accountIds.size()) {
        const auto decision = routing_.select(groupId, attemptedAccountIds);
        if (!decision) {
            break;
        }

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

                const std::string selectedModel = providerModel(request, "zai");
                if (selectedModel.empty()) {
                    lastError =
                        "Z.ai requires a provider model. Set model to a Z.ai model or provide router.models.zai.";
                    requestConfigurationError = lastError;
                    continue;
                }

                nlohmann::json outgoing = request;
                outgoing.erase("router");
                outgoing["model"] = selectedModel;
                // The initial router transport buffers provider responses even
                // when the client asks for SSE. LocalApiServer converts the
                // completed response into an OpenAI-compatible event stream.
                outgoing["stream"] = false;

                const HttpResponse response = ZaiClient::chatCompletions(
                    *apiKey,
                    outgoing.dump());

                if (response.succeeded()) {
                    routing_.recordSuccess(account.id);
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

                routing_.recordFailure(
                    account.id,
                    lastError,
                    static_cast<std::int64_t>(std::time(nullptr)));
                continue;
            }

            if (account.provider == "codex") {
                const CodexPrompt prompt = toCodexPrompt(request);
                const std::string model = providerModel(request, "codex");
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
            routing_.recordFailure(
                account.id,
                lastError,
                static_cast<std::int64_t>(std::time(nullptr)));
        }
    }

    if (requestConfigurationError) {
        return jsonError(400, *requestConfigurationError);
    }
    return jsonError(503, lastError);
}

}  // namespace routerai
