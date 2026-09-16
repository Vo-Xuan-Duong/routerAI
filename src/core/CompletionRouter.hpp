#pragma once

#include "core/RoutingManager.hpp"
#include "security/CredentialStore.hpp"
#include "storage/SQLiteDatabase.hpp"

#include <string>

namespace routerai {

struct CompletionRouteResult {
    long statusCode{500};
    std::string body;
    std::string contentType{"application/json"};
    std::string accountId;
    std::string provider;
};

class CompletionRouter {
public:
    CompletionRouter(
        SQLiteDatabase& database,
        CredentialStore& credentials,
        RoutingManager& routing);

    CompletionRouteResult chatCompletions(
        const std::string& groupId,
        const std::string& requestBody);

private:
    SQLiteDatabase& database_;
    CredentialStore& credentials_;
    RoutingManager& routing_;
};

}  // namespace routerai
