#pragma once

#include "core/RoutingManager.hpp"
#include "security/CredentialStore.hpp"
#include "storage/SQLiteDatabase.hpp"

#include <functional>
#include <string>
#include <string_view>

namespace routerai {

struct CompletionRouteResult {
    long statusCode{500};
    std::string body;
    std::string contentType{"application/json"};
    std::string accountId;
    std::string provider;
};

struct CompletionStreamResult {
    long statusCode{500};
    std::string body;
    std::string contentType{"text/event-stream"};
    std::string accountId;
    std::string provider;
    std::string streamMode{"buffered"};
    bool emittedData{false};
};

using CompletionStreamCallback = std::function<bool(std::string_view)>;

class CompletionRouter {
public:
    CompletionRouter(
        SQLiteDatabase& database,
        CredentialStore& credentials,
        RoutingManager& routing);

    CompletionRouteResult chatCompletions(
        const std::string& groupId,
        const std::string& requestBody);

    CompletionStreamResult streamChatCompletions(
        const std::string& groupId,
        const std::string& requestBody,
        const CompletionStreamCallback& onChunk);

private:
    SQLiteDatabase& database_;
    CredentialStore& credentials_;
    RoutingManager& routing_;
};

}  // namespace routerai
