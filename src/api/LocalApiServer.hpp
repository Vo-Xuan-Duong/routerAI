#pragma once

#include "core/AccountManager.hpp"
#include "core/CompletionRouter.hpp"
#include "core/RoutingManager.hpp"
#include "security/CredentialStore.hpp"
#include "storage/SQLiteDatabase.hpp"

#include <httplib.h>

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

namespace routerai {

class LocalApiServer {
public:
    LocalApiServer(
        CompletionRouter& completions,
        RoutingManager& routing,
        CredentialStore& credentials,
        SQLiteDatabase& database,
        AccountManager& accounts,
        std::string host = "127.0.0.1",
        int port = 9000);
    ~LocalApiServer();

    LocalApiServer(const LocalApiServer&) = delete;
    LocalApiServer& operator=(const LocalApiServer&) = delete;

    bool start();
    void stop();
    bool running() const noexcept { return running_; }

    const std::string& host() const noexcept { return host_; }
    int port() const noexcept { return port_; }
    std::string baseUrl() const;
    std::string adminUrl() const;
    std::string apiKey() const;
    std::string rotateApiKey();

    // Registers provider setup/login routes used by the embedded Web Admin.
    // Call once before start(). Kept separate from the core API route setup so
    // provider-management behavior remains isolated and testable.
    void configureProviderAdminRoutes();

private:
    CompletionRouter& completions_;
    RoutingManager& routing_;
    CredentialStore& credentials_;
    SQLiteDatabase& database_;
    AccountManager& accounts_;
    std::string host_;
    int port_{9000};
    std::string apiKey_;
    mutable std::mutex apiKeyMutex_;
    httplib::Server server_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    bool providerAdminRoutesConfigured_{false};

    void configureRoutes();
    void configureAdminRoutes();
    bool authorized(const httplib::Request& request) const;
    static std::string generateApiKey();
};

}  // namespace routerai
