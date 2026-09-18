#include "api/LocalApiServer.hpp"

#include <nlohmann/json.hpp>

#include <stdexcept>
#include <string>

namespace routerai {
namespace {

void setAdminJson(httplib::Response& response, int status, const nlohmann::json& body) {
    response.status = status;
    response.set_content(body.dump(), "application/json");
}

nlohmann::json accountJson(const Account& account) {
    return {
        {"id", account.id},
        {"provider", account.provider},
        {"mode", account.providerMode},
        {"display_name", account.displayName},
        {"identity", account.email.empty() ? account.displayName : account.email},
        {"status", toString(account.status)},
        {"enabled", account.enabled},
        {"priority", account.priority},
    };
}

std::string requireString(const nlohmann::json& body, const char* key) {
    const std::string value = body.value(key, std::string{});
    if (value.empty()) throw std::runtime_error(std::string(key) + " is required");
    return value;
}

}  // namespace

void LocalApiServer::configureProviderAdminRoutes() {
    if (providerAdminRoutesConfigured_) return;
    providerAdminRoutesConfigured_ = true;

    server_.Post("/admin/api/providers", [this](const httplib::Request& request, httplib::Response& response) {
        if (!authorized(request)) {
            setAdminJson(response, 401, {{"error", "Unauthorized"}});
            return;
        }

        try {
            const auto body = nlohmann::json::parse(request.body);
            const std::string provider = requireString(body, "provider");
            const std::string mode = body.value("mode", std::string{});
            const std::string apiKey = body.value("api_key", std::string{});

            Account account;
            std::string detail;
            if (provider == "codex") {
                account = accounts_.addCodexAccount();
                detail = "Codex account created. Use browser login to authenticate it.";
            } else if (provider == "antigravity") {
                if (mode == "consumer-cli") {
                    account = accounts_.addAntigravityAccount();
                    detail = "Antigravity consumer session added. Use browser login to authenticate the official CLI session.";
                } else if (mode == "api-project") {
                    account = accounts_.addAntigravityApiProject();
                    if (!apiKey.empty()) {
                        const auto outcome = accounts_.configureAntigravityApiKey(account.id, apiKey);
                        account = outcome.account;
                        detail = outcome.auth.detail;
                    } else {
                        detail = "Antigravity API project added. Configure a Gemini API key before routing requests.";
                    }
                } else {
                    throw std::runtime_error("Antigravity mode must be consumer-cli or api-project");
                }
            } else if (provider == "zai") {
                const std::string effectiveMode = mode.empty() ? "general-api" : mode;
                if (effectiveMode != "general-api" && effectiveMode != "coding-plan") {
                    throw std::runtime_error("Z.ai mode must be general-api or coding-plan");
                }
                account = accounts_.addZaiAccount(effectiveMode);
                if (!apiKey.empty()) {
                    const auto outcome = accounts_.configureZaiApiKey(account.id, apiKey, effectiveMode);
                    account = outcome.account;
                    detail = outcome.auth.detail;
                } else {
                    detail = "Z.ai account added. Configure its API key before use.";
                }
            } else {
                throw std::runtime_error("Unsupported provider: " + provider);
            }

            routing_.syncDefaultGroups();
            setAdminJson(response, 201, {{"ok", true}, {"account", accountJson(account)}, {"detail", detail}});
        } catch (const std::exception& exception) {
            setAdminJson(response, 400, {{"error", exception.what()}});
        }
    });

    server_.Post("/admin/api/provider-key", [this](const httplib::Request& request, httplib::Response& response) {
        if (!authorized(request)) {
            setAdminJson(response, 401, {{"error", "Unauthorized"}});
            return;
        }

        try {
            const auto body = nlohmann::json::parse(request.body);
            const std::string id = requireString(body, "id");
            const std::string apiKey = requireString(body, "api_key");
            const auto current = accounts_.findAccount(id);
            if (!current) throw std::runtime_error("Account not found: " + id);

            Account account;
            std::string detail;
            if (current->provider == "antigravity" && current->providerMode == "api-project") {
                const auto outcome = accounts_.configureAntigravityApiKey(id, apiKey);
                account = outcome.account;
                detail = outcome.auth.detail;
            } else if (current->provider == "zai") {
                const std::string mode = current->providerMode.empty() ? "general-api" : current->providerMode;
                const auto outcome = accounts_.configureZaiApiKey(id, apiKey, mode);
                account = outcome.account;
                detail = outcome.auth.detail;
            } else {
                throw std::runtime_error("This account does not use an API-key setup flow");
            }

            routing_.syncDefaultGroups();
            setAdminJson(response, 200, {{"ok", true}, {"account", accountJson(account)}, {"detail", detail}});
        } catch (const std::exception& exception) {
            setAdminJson(response, 400, {{"error", exception.what()}});
        }
    });

    server_.Post("/admin/api/login", [this](const httplib::Request& request, httplib::Response& response) {
        if (!authorized(request)) {
            setAdminJson(response, 401, {{"error", "Unauthorized"}});
            return;
        }

        try {
            const auto body = nlohmann::json::parse(request.body);
            const std::string id = requireString(body, "id");
            const bool useBrowser = body.value("use_browser", true);
            const auto outcome = accounts_.loginAccount(id, useBrowser);
            routing_.syncDefaultGroups();
            setAdminJson(
                response,
                outcome.result.success ? 200 : 409,
                {{"ok", outcome.result.success}, {"account", accountJson(outcome.account)}, {"detail", outcome.result.detail}});
        } catch (const std::exception& exception) {
            setAdminJson(response, 400, {{"error", exception.what()}});
        }
    });

    server_.Post("/admin/api/refresh-account", [this](const httplib::Request& request, httplib::Response& response) {
        if (!authorized(request)) {
            setAdminJson(response, 401, {{"error", "Unauthorized"}});
            return;
        }

        try {
            const auto body = nlohmann::json::parse(request.body);
            const std::string id = requireString(body, "id");
            const auto outcome = accounts_.refreshAccountStatus(id);
            if (outcome.auth.authenticated && (outcome.account.provider == "codex" || (outcome.account.provider == "antigravity" && outcome.account.providerMode == "consumer-cli"))) {
                try {
                    accounts_.readQuota(id);
                } catch (...) {}
            }
            routing_.syncDefaultGroups();
            setAdminJson(response, 200, {{"ok", true}, {"account", accountJson(outcome.account)}, {"detail", outcome.auth.detail}});
        } catch (const std::exception& exception) {
            setAdminJson(response, 400, {{"error", exception.what()}});
        }
    });
}

}  // namespace routerai
