#include "api/LocalApiServer.hpp"
#include "core/AccountManager.hpp"
#include "core/CompletionRouter.hpp"
#include "core/RoutingManager.hpp"
#include "security/CredentialStore.hpp"
#include "storage/SQLiteDatabase.hpp"
#include "ui/ManagementApp.hpp"

#include <spdlog/spdlog.h>

#include <exception>

int main() {
    try {
        routerai::SQLiteDatabase database("router.db");
        database.initialize();

        routerai::CredentialStore credentials;
        routerai::AccountManager accounts(database, credentials);
        routerai::RoutingManager routing(database);
        routing.syncDefaultGroups();
        routerai::CompletionRouter completions(database, credentials, routing);
        routerai::LocalApiServer api(
            completions,
            routing,
            credentials,
            database,
            accounts);
        api.configureProviderAdminRoutes();

        if (!api.start()) {
            spdlog::warn("Local API could not bind to {}:{}", api.host(), api.port());
        } else {
            spdlog::info("Local API listening at {}", api.baseUrl());
            spdlog::info("Web Admin available at {}", api.adminUrl());
        }

        routerai::ManagementApp app(database, accounts, routing, api);
        const int exitCode = app.run();
        api.stop();
        return exitCode;
    } catch (const std::exception& exception) {
        spdlog::error("{}", exception.what());
        return 1;
    }
}
