#include "api/LocalApiServer.hpp"

#include "api/AdminWebUi.hpp"
#include "api/OpenAICompat.hpp"
#include "core/ConfigManager.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <iomanip>
#include <memory>
#include <optional>
#include <random>
#include <sstream>
#include <utility>

namespace routerai {

namespace {

constexpr const char* localApiCredentialRef = "router-local-api-key";

void setJson(httplib::Response& response, int status, const nlohmann::json& body) {
    response.status = status;
    response.set_content(body.dump(), "application/json");
}

std::string streamErrorEvent(const CompletionStreamResult& result) {
    nlohmann::json payload;
    try {
        payload = nlohmann::json::parse(result.body);
    } catch (...) {
        payload = {
            {"error",
             {
                 {"message", result.body.empty() ? "Streaming request failed" : result.body},
                 {"type", "router_stream_error"},
             }},
        };
    }
    return "event: error\ndata: " + payload.dump() + "\n\n";
}

std::int64_t elapsedMs(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
}

std::string boundedError(std::string value) {
    constexpr std::size_t maxLength = 600;
    if (value.size() > maxLength) value.resize(maxLength);
    return value;
}

std::optional<double> latestUsage(const std::vector<QuotaHistoryEntry>& history) {
    if (history.empty()) return std::nullopt;
    const std::int64_t snapshot = history.front().snapshotId;
    std::optional<double> highest;
    for (const auto& item : history) {
        if (item.snapshotId != snapshot) break;
        if (!highest || item.usedPercent > *highest) highest = item.usedPercent;
    }
    return highest;
}

std::string requestModel(const nlohmann::json& request) {
    if (request.is_object() && request.contains("model") && request.at("model").is_string()) {
        return request.at("model").get<std::string>();
    }
    return {};
}

}  // namespace

LocalApiServer::LocalApiServer(
    CompletionRouter& completions,
    RoutingManager& routing,
    CredentialStore& credentials,
    SQLiteDatabase& database,
    AccountManager& accounts,
    std::string host,
    int port)
    : completions_(completions),
      routing_(routing),
      credentials_(credentials),
      database_(database),
      accounts_(accounts),
      host_(std::move(host)),
      port_(port) {
    if (const auto existing = credentials_.get(localApiCredentialRef)) {
        apiKey_ = *existing;
    } else {
        apiKey_ = generateApiKey();
        credentials_.put(localApiCredentialRef, apiKey_);
    }
    configureRoutes();
    configureAdminRoutes();
}

LocalApiServer::~LocalApiServer() {
    stop();
}

bool LocalApiServer::start() {
    if (running_) return true;
    if (!server_.bind_to_port(host_, port_)) return false;

    running_ = true;
    thread_ = std::thread([this] {
        server_.listen_after_bind();
        running_ = false;
    });
    return true;
}

void LocalApiServer::stop() {
    if (running_) server_.stop();
    if (thread_.joinable()) thread_.join();
    running_ = false;
}

std::string LocalApiServer::baseUrl() const {
    return "http://" + host_ + ':' + std::to_string(port_) + "/v1";
}

std::string LocalApiServer::adminUrl() const {
    return "http://" + host_ + ':' + std::to_string(port_) + "/admin";
}

std::string LocalApiServer::apiKey() const {
    std::lock_guard<std::mutex> lock(apiKeyMutex_);
    return apiKey_;
}

std::string LocalApiServer::rotateApiKey() {
    const std::string replacement = generateApiKey();
    credentials_.put(localApiCredentialRef, replacement);
    {
        std::lock_guard<std::mutex> lock(apiKeyMutex_);
        apiKey_ = replacement;
    }
    return replacement;
}

bool LocalApiServer::authorized(const httplib::Request& request) const {
    const std::string expected = "Bearer " + apiKey();
    return request.get_header_value("Authorization") == expected;
}

void LocalApiServer::configureRoutes() {
    server_.Get("/health", [this](const httplib::Request&, httplib::Response& response) {
        setJson(response, 200, {{"status", "ok"}, {"service", "routerAI"}, {"api", baseUrl()}, {"admin", adminUrl()}});
    });

    server_.Get("/v1/models", [this](const httplib::Request& request, httplib::Response& response) {
        if (!authorized(request)) {
            setJson(response, 401, {{"error", {{"message", "Unauthorized"}, {"type", "authentication_error"}}}});
            return;
        }

        nlohmann::json data = nlohmann::json::array();
        for (const auto& group : routing_.listGroups()) {
            if (!group.enabled || !routing_.groupSupportsCompletions(group)) continue;
            data.push_back({{"id", "router/" + group.id}, {"object", "model"}, {"owned_by", "routerAI"}});
        }
        setJson(response, 200, {{"object", "list"}, {"data", std::move(data)}});
    });

    server_.Post("/v1/chat/completions", [this](const httplib::Request& request, httplib::Response& response) {
        if (!authorized(request)) {
            setJson(response, 401, {{"error", {{"message", "Unauthorized"}, {"type", "authentication_error"}}}});
            return;
        }

        nlohmann::json parsedRequest;
        bool wantsStreaming = false;
        std::string groupId;
        std::string model;
        try {
            parsedRequest = nlohmann::json::parse(request.body);
            wantsStreaming = openai_compat::wantsStreaming(parsedRequest);
            groupId = openai_compat::resolveRoutingGroup(parsedRequest, request.get_header_value("X-Router-Group"));
            model = requestModel(parsedRequest);
        } catch (...) {
            groupId = request.get_header_value("X-Router-Group");
            if (groupId.empty()) groupId = "mixed-default";
        }

        const auto startedAt = std::chrono::steady_clock::now();
        if (wantsStreaming) {
            const auto group = routing_.findGroup(groupId);
            if (!group) {
                setJson(response, 404, {{"error", {{"message", "Routing group not found: " + groupId}, {"type", "router_error"}}}});
                return;
            }
            if (!group->enabled) {
                setJson(response, 503, {{"error", {{"message", "Routing group is disabled: " + groupId}, {"type", "router_error"}}}});
                return;
            }

            response.status = 200;
            response.set_header("Cache-Control", "no-cache");
            response.set_header("X-Router-Group", groupId);
            response.set_header("X-Router-Stream-Mode", "native-or-buffered");

            const std::string body = request.body;
            auto started = std::make_shared<bool>(false);
            response.set_chunked_content_provider(
                "text/event-stream",
                [this, body, groupId, model, started, startedAt](std::size_t, httplib::DataSink& sink) mutable {
                    if (*started) {
                        sink.done();
                        return true;
                    }
                    *started = true;

                    const CompletionStreamResult result = completions_.streamChatCompletions(
                        groupId,
                        body,
                        [&](std::string_view chunk) { return sink.write(chunk.data(), chunk.size()); });

                    if ((result.statusCode < 200 || result.statusCode >= 300) && !result.emittedData) {
                        const std::string errorEvent = streamErrorEvent(result);
                        sink.write(errorEvent.data(), errorEvent.size());
                        const std::string done = "data: [DONE]\n\n";
                        sink.write(done.data(), done.size());
                    }

                    RequestLogEntry log;
                    log.groupId = groupId;
                    log.accountId = result.accountId;
                    log.provider = result.provider;
                    log.model = model;
                    log.statusCode = result.statusCode;
                    log.durationMs = elapsedMs(startedAt);
                    log.streaming = true;
                    log.success = result.statusCode >= 200 && result.statusCode < 300;
                    if (!log.success) log.error = boundedError(result.body);
                    try { database_.recordRequestLog(log); } catch (...) {}

                    sink.done();
                    return true;
                });
            return;
        }

        const CompletionRouteResult result = completions_.chatCompletions(groupId, request.body);
        response.status = static_cast<int>(result.statusCode);
        response.set_content(result.body, result.contentType.empty() ? "application/json" : result.contentType);
        if (!result.accountId.empty()) response.set_header("X-Router-Account", result.accountId);
        if (!result.provider.empty()) response.set_header("X-Router-Provider", result.provider);
        response.set_header("X-Router-Group", groupId);

        RequestLogEntry log;
        log.groupId = groupId;
        log.accountId = result.accountId;
        log.provider = result.provider;
        log.model = model;
        log.statusCode = result.statusCode;
        log.durationMs = elapsedMs(startedAt);
        log.success = result.statusCode >= 200 && result.statusCode < 300;
        if (!log.success) log.error = boundedError(result.body);
        try { database_.recordRequestLog(log); } catch (...) {}
    });
}

void LocalApiServer::configureAdminRoutes() {
    server_.Get("/admin", [](const httplib::Request&, httplib::Response& response) {
        response.set_header(
            "Content-Security-Policy",
            "default-src 'self'; connect-src 'self'; style-src 'unsafe-inline'; script-src 'unsafe-inline'");
        response.set_content(std::string(adminWebUiHtml), "text/html; charset=utf-8");
    });

    server_.Get("/admin/api/overview", [this](const httplib::Request& request, httplib::Response& response) {
        if (!authorized(request)) { setJson(response, 401, {{"error", "Unauthorized"}}); return; }

        nlohmann::json accountData = nlohmann::json::array();
        std::size_t ready = 0, warning = 0;
        for (const auto& account : accounts_.listAccounts()) {
            if (account.status == AccountStatus::Ready && account.enabled) ++ready;
            if (account.status == AccountStatus::Warning && account.enabled) ++warning;
            const auto usage = latestUsage(accounts_.listQuotaHistory(account.id, 100));
            accountData.push_back({
                {"id", account.id},
                {"provider", account.provider},
                {"mode", account.providerMode},
                {"status", toString(account.status)},
                {"enabled", account.enabled},
                {"identity", account.email.empty() ? account.displayName : account.email},
                {"usage", usage ? nlohmann::json(*usage) : nlohmann::json(nullptr)},
            });
        }

        nlohmann::json groupData = nlohmann::json::array();
        for (const auto& group : routing_.listGroups()) {
            groupData.push_back({
                {"id", group.id}, {"strategy", toString(group.strategy)},
                {"enabled", group.enabled}, {"members", group.accountIds.size()},
            });
        }

        const auto logs = database_.listRequestLogs(2000);
        std::size_t successful = 0;
        for (const auto& log : logs) if (log.success) ++successful;
        const double successRate = logs.empty() ? 100.0 : 100.0 * static_cast<double>(successful) / static_cast<double>(logs.size());

        setJson(response, 200, {
            {"summary", {
                {"accounts", accountData.size()}, {"ready", ready}, {"warning", warning},
                {"requests", logs.size()}, {"success_rate", successRate},
            }},
            {"accounts", std::move(accountData)}, {"groups", std::move(groupData)},
        });
    });

    server_.Get("/admin/api/requests", [this](const httplib::Request& request, httplib::Response& response) {
        if (!authorized(request)) { setJson(response, 401, {{"error", "Unauthorized"}}); return; }
        std::size_t limit = 200;
        if (request.has_param("limit")) {
            try { limit = static_cast<std::size_t>(std::stoul(request.get_param_value("limit"))); } catch (...) {}
        }
        nlohmann::json data = nlohmann::json::array();
        for (const auto& entry : database_.listRequestLogs(limit)) {
            data.push_back({
                {"id", entry.id}, {"created_at", entry.createdAt}, {"group", entry.groupId},
                {"account", entry.accountId}, {"provider", entry.provider}, {"model", entry.model},
                {"status", entry.statusCode}, {"duration_ms", entry.durationMs},
                {"streaming", entry.streaming}, {"success", entry.success}, {"error", entry.error},
            });
        }
        setJson(response, 200, {{"data", std::move(data)}});
    });

    server_.Delete("/admin/api/requests", [this](const httplib::Request& request, httplib::Response& response) {
        if (!authorized(request)) { setJson(response, 401, {{"error", "Unauthorized"}}); return; }
        database_.clearRequestLogs();
        setJson(response, 200, {{"ok", true}});
    });

    server_.Post("/admin/api/account-action", [this](const httplib::Request& request, httplib::Response& response) {
        if (!authorized(request)) { setJson(response, 401, {{"error", "Unauthorized"}}); return; }
        try {
            const auto body = nlohmann::json::parse(request.body);
            const std::string id = body.value("id", std::string{});
            const std::string action = body.value("action", std::string{});
            if (id.empty()) throw std::runtime_error("Account id is required");
            if (action == "enable") accounts_.setAccountEnabled(id, true);
            else if (action == "disable") accounts_.setAccountEnabled(id, false);
            else if (action == "remove") accounts_.removeAccount(id);
            else throw std::runtime_error("Unsupported account action");
            routing_.syncDefaultGroups();
            setJson(response, 200, {{"ok", true}});
        } catch (const std::exception& exception) {
            setJson(response, 400, {{"error", exception.what()}});
        }
    });

    server_.Get("/admin/api/config", [this](const httplib::Request& request, httplib::Response& response) {
        if (!authorized(request)) { setJson(response, 401, {{"error", "Unauthorized"}}); return; }
        ConfigManager configs(database_, routing_);
        response.set_header("Content-Disposition", "attachment; filename=routerai-config.json");
        response.set_content(configs.exportJson(), "application/json");
    });

    server_.Post("/admin/api/config", [this](const httplib::Request& request, httplib::Response& response) {
        if (!authorized(request)) { setJson(response, 401, {{"error", "Unauthorized"}}); return; }
        try {
            ConfigManager configs(database_, routing_);
            const auto result = configs.importJson(request.body);
            setJson(response, 200, {{"ok", true}, {"accounts", result.accounts}, {"groups", result.routingGroups}, {"detail", result.detail}});
        } catch (const std::exception& exception) {
            setJson(response, 400, {{"error", exception.what()}});
        }
    });
}

std::string LocalApiServer::generateApiKey() {
    std::random_device random;
    std::array<unsigned char, 24> bytes{};
    for (auto& byte : bytes) byte = static_cast<unsigned char>(random());

    std::ostringstream output;
    output << "router-local-" << std::hex << std::setfill('0');
    for (const unsigned char byte : bytes) output << std::setw(2) << static_cast<unsigned int>(byte);
    return output.str();
}

}  // namespace routerai
