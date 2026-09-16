#pragma once

#include "core/CompletionRouter.hpp"
#include "core/RoutingManager.hpp"
#include "security/CredentialStore.hpp"

#include <httplib.h>

#include <atomic>
#include <string>
#include <thread>

namespace routerai {

class LocalApiServer {
public:
    LocalApiServer(
        CompletionRouter& completions,
        RoutingManager& routing,
        CredentialStore& credentials,
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
    const std::string& apiKey() const noexcept { return apiKey_; }

private:
    CompletionRouter& completions_;
    RoutingManager& routing_;
    CredentialStore& credentials_;
    std::string host_;
    int port_{9000};
    std::string apiKey_;
    httplib::Server server_;
    std::thread thread_;
    std::atomic<bool> running_{false};

    void configureRoutes();
    bool authorized(const httplib::Request& request) const;
    static std::string generateApiKey();
};

}  // namespace routerai
