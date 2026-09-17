#include "api/LocalApiServer.hpp"
#include "core/AccountManager.hpp"
#include "core/CompletionRouter.hpp"
#include "core/RoutingManager.hpp"
#include "security/CredentialStore.hpp"
#include "storage/SQLiteDatabase.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

bool hasModel(const nlohmann::json& response, const std::string& id) {
    const auto data = response.find("data");
    if (data == response.end() || !data->is_array()) return false;
    for (const auto& item : *data) {
        if (item.is_object() && item.value("id", std::string{}) == id) return true;
    }
    return false;
}

}  // namespace

int main() {
    const std::filesystem::path root = "local-api-test-runtime";
    const std::filesystem::path databasePath = root / "router.db";
    const std::filesystem::path secretPath = root / "secrets";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root);

    try {
        routerai::SQLiteDatabase database(databasePath.string());
        database.initialize();

        routerai::Account zai;
        zai.id = "zai-test";
        zai.provider = "zai";
        zai.providerMode = "general-api";
        zai.displayName = "Z.ai test";
        zai.status = routerai::AccountStatus::Ready;
        zai.enabled = true;
        database.insertAccount(zai);

        routerai::CredentialStore credentials(secretPath);
        routerai::AccountManager accounts(database, credentials);
        routerai::RoutingManager routing(database);
        routing.syncDefaultGroups();
        routerai::CompletionRouter completions(database, credentials, routing);

        constexpr int port = 19091;
        routerai::LocalApiServer server(
            completions,
            routing,
            credentials,
            database,
            accounts,
            "127.0.0.1",
            port);
        require(server.start(), "local API server failed to bind test port");
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        httplib::Client client("127.0.0.1", port);
        client.set_connection_timeout(2, 0);
        client.set_read_timeout(2, 0);

        const auto health = client.Get("/health");
        require(health && health->status == 200, "GET /health must succeed without auth");
        const auto healthJson = nlohmann::json::parse(health->body);
        require(healthJson.value("status", std::string{}) == "ok", "health payload mismatch");
        require(healthJson.value("admin", std::string{}) == server.adminUrl(), "admin URL missing from health");

        const auto adminPage = client.Get("/admin");
        require(adminPage && adminPage->status == 200, "Web Admin page must be locally readable");
        require(adminPage->body.find("routerAI Admin") != std::string::npos, "Web Admin HTML missing");

        const auto unauthorized = client.Get("/v1/models");
        require(unauthorized && unauthorized->status == 401, "GET /v1/models must require auth");
        const auto adminUnauthorized = client.Get("/admin/api/overview");
        require(adminUnauthorized && adminUnauthorized->status == 401, "Admin API must require auth");

        httplib::Headers headers = {{"Authorization", "Bearer " + server.apiKey()}};
        const auto models = client.Get("/v1/models", headers);
        require(models && models->status == 200, "authorized GET /v1/models must succeed");
        const auto modelJson = nlohmann::json::parse(models->body);
        require(hasModel(modelJson, "router/zai-default"), "Z.ai executable group missing from model list");
        require(hasModel(modelJson, "router/mixed-default"), "mixed executable group missing from model list");
        require(!hasModel(modelJson, "router/antigravity-default"), "consumer-only group must not be advertised");

        const auto overview = client.Get("/admin/api/overview", headers);
        require(overview && overview->status == 200, "authenticated admin overview must succeed");

        const nlohmann::json streamingRequest = {
            {"model", "router/zai-default"},
            {"stream", true},
            {"router", {{"models", {{"zai", "glm-5.2"}}}}},
            {"messages", nlohmann::json::array({{{"role", "user"}, {"content", "hello"}}})},
        };
        const auto streaming = client.Post(
            "/v1/chat/completions",
            headers,
            streamingRequest.dump(),
            "application/json");
        require(streaming && streaming->status == 200, "stream request must establish an SSE response");
        require(streaming->get_header_value("Content-Type").find("text/event-stream") != std::string::npos,
                "stream response must use text/event-stream");
        require(streaming->body.find("event: error") != std::string::npos,
                "missing credential must be represented as an SSE error event");
        require(streaming->body.ends_with("data: [DONE]\n\n"),
                "SSE error stream must terminate with [DONE]");

        const auto requestHistory = client.Get("/admin/api/requests?limit=20", headers);
        require(requestHistory && requestHistory->status == 200, "request history endpoint must succeed");
        const auto historyJson = nlohmann::json::parse(requestHistory->body);
        require(historyJson.at("data").is_array() && !historyJson.at("data").empty(), "stream request must be logged");
        require(historyJson.at("data").front().value("model", std::string{}) == "router/zai-default", "request model metadata mismatch");

        const nlohmann::json disableAction = {{"id", "zai-test"}, {"action", "disable"}};
        const auto disabled = client.Post("/admin/api/account-action", headers, disableAction.dump(), "application/json");
        require(disabled && disabled->status == 200, "admin disable action must succeed");
        require(!database.findAccount("zai-test")->enabled, "admin disable action did not persist");

        const auto configExport = client.Get("/admin/api/config", headers);
        require(configExport && configExport->status == 200, "admin config export must succeed");
        require(configExport->body.find("credential_ref") == std::string::npos, "admin config export leaked credential field");

        const std::string oldKey = server.apiKey();
        const std::string newKey = server.rotateApiKey();
        require(oldKey != newKey, "rotating local API key must generate a new key");
        const auto oldKeyResult = client.Get("/v1/models", httplib::Headers{{"Authorization", "Bearer " + oldKey}});
        require(oldKeyResult && oldKeyResult->status == 401, "old local API key must be invalid after rotation");
        const auto newKeyResult = client.Get("/v1/models", httplib::Headers{{"Authorization", "Bearer " + newKey}});
        require(newKeyResult && newKeyResult->status == 200, "new local API key must work immediately");

        server.stop();
        std::filesystem::remove_all(root, ignored);
        std::cout << "LocalApiServerTests: OK\n";
    } catch (const std::exception& exception) {
        std::cerr << "LocalApiServerTests: FAILED: " << exception.what() << '\n';
        std::filesystem::remove_all(root, ignored);
        return 1;
    }

    return 0;
}
