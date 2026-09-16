#include "api/LocalApiServer.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <cstdint>
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

bool requestWantsStreaming(const std::string& body) {
    try {
        const auto json = nlohmann::json::parse(body);
        return json.is_object() && json.value("stream", false);
    } catch (...) {
        return false;
    }
}

std::string bufferedSse(const std::string& responseBody) {
    const auto full = nlohmann::json::parse(responseBody);
    if (!full.is_object() || !full.contains("choices") || !full.at("choices").is_array() || full.at("choices").empty()) {
        throw std::runtime_error("Provider response cannot be converted to chat completion SSE");
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

    const std::string id = full.value("id", std::string("chatcmpl-router-buffered"));
    const std::string model = full.value("model", std::string("router"));
    const std::int64_t created = full.value("created", static_cast<std::int64_t>(0));

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
            if (!group.enabled) {
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

        const bool wantsStreaming = requestWantsStreaming(request.body);
        std::string groupId = request.get_header_value("X-Router-Group");
        if (groupId.empty()) {
            try {
                const auto body = nlohmann::json::parse(request.body);
                const auto router = body.find("router");
                if (router != body.end() && router->is_object()) {
                    groupId = router->value("group", std::string{});
                }
                if (groupId.empty()) {
                    const std::string model = body.value("model", std::string{});
                    constexpr const char* prefix = "router/";
                    if (model.starts_with(prefix)) {
                        groupId = model.substr(std::char_traits<char>::length(prefix));
                    }
                }
            } catch (...) {
                // CompletionRouter returns the detailed JSON parse error.
            }
        }
        if (groupId.empty()) {
            groupId = "mixed-default";
        }

        const CompletionRouteResult result = completions_.chatCompletions(groupId, request.body);
        response.status = static_cast<int>(result.statusCode);

        if (wantsStreaming && result.statusCode >= 200 && result.statusCode < 300) {
            try {
                response.set_content(bufferedSse(result.body), "text/event-stream");
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
