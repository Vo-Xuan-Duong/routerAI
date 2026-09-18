#pragma once

#include "core/RequestLog.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace routerai {

class CredentialStore;
class RoutingManager;
class SQLiteDatabase;

struct MaintenanceReport {
    std::uintmax_t databaseBytes{0};
    std::size_t requestLogRows{0};
    std::size_t missingCredentialRefs{0};
    std::size_t invalidRoutingMembers{0};
    std::size_t orphanRuntimeDirectories{0};
    std::size_t missingRuntimeDirectories{0};

    std::vector<std::string> missingCredentialAccounts;
    std::vector<std::string> invalidRoutingEntries;
    std::vector<std::filesystem::path> orphanRuntimePaths;
    std::vector<std::string> missingRuntimeAccounts;
};

class MaintenanceManager {
public:
    MaintenanceManager(
        SQLiteDatabase& database,
        RoutingManager& routing,
        const CredentialStore& credentials,
        std::filesystem::path runtimeAccountsRoot =
            std::filesystem::path(".routerai") / "accounts");

    MaintenanceReport inspect() const;
    RequestLogMaintenanceResult pruneRequestLogs(
        const RequestLogRetentionPolicy& policy = {});
    std::size_t repairRoutingGroups();
    std::size_t removeOrphanRuntimeDirectories();

private:
    SQLiteDatabase& database_;
    RoutingManager& routing_;
    const CredentialStore& credentials_;
    std::filesystem::path runtimeAccountsRoot_;
};

}  // namespace routerai
