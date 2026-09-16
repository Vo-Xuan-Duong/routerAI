#include "api/LocalApiServer.hpp"

#include "api/OpenAICompat.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <iomanip>
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

}  // namespace

LocalApiServer::LocalApiServer(
    CompletionRouter& completions,
    RoutingManager& routing,
    CredentialStore& credentials,
    std::string host,
    int port)
    : completions_(completions),
      routing_(routing),
      credentials_(credentials),
      host_(std::move(host)),
      port_(port) {
    if (const auto existing = credentials_.get(localApiCredentialRef)) {
        apiKey_ = *existing;
    } else {
        apiKey_ = generateApiKey();
        credentials_.put(localApiCredentialRef, apiKey_);
    }
    configureRoutes();
}

LocalApiServer::~LocalApiServer() {
    stop();
}

bool LocalApiServer::start() {
    if (running_) {
        return true;
    }
    if (!server_.bind_to_port(host_, port_)) {
        return false;
    }

    running_ = true;
    thread_ = std::thread([this] {
        server_.listen_after_bind();
        running_ = false;
    });
    return true;
}

void LocalApiServer::stop() {
    if (running_) {
        server_.stop();
    }
    if (thread_.joinable()) {
        thread_.join();
    }
    running_ = false;
}

std::string LocalApiServer::baseUrl() const {
    return "http://" + host_ + ':' + std::to_string(port_) + "/v1";
}

bool LocalApiServer::authorized(const httplib::Request& request) const {
    const std::string expected = "Bearer " + apiKey_;
    return request.get_header_value("Authorization") == expected;
}

void LocalApiServer::configureRoutes() {
    server_.Get("/health", [this](const httplib::Request&, httplib::Response& response) {
        setJson(
            response,
            200,
            {
                {"status", "ok"},
                {"service", "routerAI"},
                {"api", baseUrl()},
            });
    });

    server_.Get("/v1/models", [this](const httplib::Request& request, httplib::Response& response) {
        if (!authorized(request)) {
            setJson(response, 401, {{"error", {{"message", "Unauthorized"}, {"type", "authentication_error"}}}});
            return;
        }

        nlohmann::json data = nlohmann::json::array();
        for (const auto& group : routing_.listGroups()) {
            if (!group.enabled || !routing_.groupSupportsCompletions(group)) {
                continue;
            }
            data.push_back({
                {"id", "router/" + group.id},
                {"object", "model"},
                {"owned_by", "routerAI"},
            });
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
        try {
            parsedRequest = nlohmann::json::parse(request.body);
            wantsStreaming = openai_compat::wantsStreaming(parsedRequest);
            groupId = openai_compat::resolveRoutingGroup(
                parsedRequest,
                request.get_header_value("X-Router-Group"));
        } catch (...) {
            // CompletionRouter returns the detailed JSON parse error.
            groupId = request.get_header_value("X-Router-Group");
            if (groupId.empty()) {
                groupId = "mixed-default";
            }
        }

        const CompletionRouteResult result = completions_.chatCompletions(groupId, request.body);
        response.status = static_cast<int>(result.statusCode);

        if (wantsStreaming && result.statusCode >= 200 && result.statusCode < 300) {
            try {
                const auto full = nlohmann::json::parse(result.body);
                response.set_content(
                    openai_compat::bufferedChatCompletionSse(full),
                    "text/event-stream");
                response.set_header("Cache-Control", "no-cache");
                response.set_header("X-Router-Stream-Mode", "buffered");
            } catch (const std::exception& exception) {
                setJson(
                    response,
                    502,
                    {{"error", {{"message", exception.what()}, {"type", "router_stream_error"}}}});
            }
        } else {
            response.set_content(
                result.body,
                result.contentType.empty() ? "application/json" : result.contentType);
        }

        if (!result.accountId.empty()) {
            response.set_header("X-Router-Account", result.accountId);
        }
        if (!result.provider.empty()) {
            response.set_header("X-Router-Provider", result.provider);
        }
        response.set_header("X-Router-Group", groupId);
    });
}

std::string LocalApiServer::generateApiKey() {
    std::random_device random;
    std::array<unsigned char, 24> bytes{};
    for (auto& byte : bytes) {
        byte = static_cast<unsigned char>(random());
    }

    std::ostringstream output;
    output << "router-local-" << std::hex << std::setfill('0');
    for (const unsigned char byte : bytes) {
        output << std::setw(2) << static_cast<unsigned int>(byte);
    }
    return output.str();
}

}  // namespace routerai
