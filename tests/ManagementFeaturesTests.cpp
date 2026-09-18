#include "core/ConfigManager.hpp"
#include "core/MaintenanceManager.hpp"
#include "core/RequestLog.hpp"
#include "core/RoutingManager.hpp"
#include "storage/SQLiteDatabase.hpp"
#include "security/CredentialStore.hpp"

#include <filesystem>
#include <sqlite3.h>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main() {
    const std::filesystem::path root = "management-features-test-runtime";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root);

    try {
        routerai::SQLiteDatabase db((root / "source.db").string());
        db.initialize();

        routerai::Account account;
        account.id = "zai-export";
        account.provider = "zai";
        account.providerMode = "general-api";
        account.displayName = "Z.ai export";
        account.credentialRef = "must-not-export";
        account.status = routerai::AccountStatus::Ready;
        account.priority = 42;
        db.insertAccount(account);

        routerai::RoutingManager routing(db);
        routing.syncDefaultGroups();
        routerai::ConfigManager configs(db, routing);
        const std::string exported = configs.exportJson();
        require(exported.find("must-not-export") == std::string::npos, "credential reference leaked into config export");
        require(exported.find("credential_ref") == std::string::npos, "credential_ref field must not be exported");

        const std::string rebindConfig = R"JSON({
          "schema_version": 1,
          "accounts": [
            {
              "id": "zai-export",
              "provider": "antigravity",
              "provider_mode": "api-project",
              "display_name": "Rebound",
              "priority": 999
            }
          ],
          "routing_groups": []
        })JSON";
        const auto rebindResult = configs.importJson(rebindConfig);
        require(rebindResult.accounts == 0, "existing account provider identity must be immutable during import");
        const auto afterRebind = db.findAccount("zai-export");
        require(afterRebind.has_value(), "source account disappeared after rebind attempt");
        require(afterRebind->provider == "zai", "config import changed existing account provider");
        require(afterRebind->providerMode == "general-api", "config import changed existing account provider mode");
        require(afterRebind->credentialRef == "must-not-export", "existing credential reference changed during import");
        require(afterRebind->priority == 42, "rejected rebind must not partially update metadata");

        routerai::RequestLogEntry log;
        log.groupId = "zai-default";
        log.accountId = account.id;
        log.provider = "zai";
        log.model = "glm-5.2";
        log.statusCode = 200;
        log.durationMs = 123;
        log.success = true;
        db.recordRequestLog(log);
        const auto logs = db.listRequestLogs();
        require(logs.size() == 1, "request history must persist");
        require(logs.front().durationMs == 123, "request duration mismatch");
        db.clearRequestLogs();
        require(db.listRequestLogs().empty(), "request history clear failed");

        routerai::SQLiteDatabase importedDb((root / "imported.db").string());
        importedDb.initialize();
        routerai::RoutingManager importedRouting(importedDb);
        routerai::ConfigManager importedConfigs(importedDb, importedRouting);
        const auto result = importedConfigs.importJson(exported);
        require(result.accounts == 1, "one account should import");
        const auto imported = importedDb.findAccount(account.id);
        require(imported.has_value(), "imported account missing");
        require(imported->credentialRef.empty(), "import must never restore credential reference");
        require(imported->status == routerai::AccountStatus::AuthExpired, "new imported credential account must require auth");
        require(imported->priority == 42, "account priority was not restored");

        const std::string maliciousConfig = R"JSON({
          "schema_version": 1,
          "accounts": [
            {
              "id": "safe-import",
              "provider": "zai",
              "provider_mode": "general-api",
              "display_name": "Safe",
              "credential_ref": "attacker-controlled-ref"
            },
            {
              "id": "../../escape",
              "provider": "codex",
              "display_name": "Unsafe"
            },
            {
              "id": "unknown-provider",
              "provider": "evil-provider",
              "provider_mode": "api-project",
              "display_name": "Unsafe provider"
            }
          ],
          "routing_groups": [
            {
              "id": "../../unsafe-group",
              "display_name": "Unsafe",
              "strategy": "MANUAL",
              "account_ids": ["safe-import"]
            }
          ]
        })JSON";
        const auto maliciousResult = importedConfigs.importJson(maliciousConfig);
        require(maliciousResult.accounts == 1, "unsafe account/provider identifiers must be skipped");
        const auto safeImported = importedDb.findAccount("safe-import");
        require(safeImported.has_value(), "safe account should import");
        require(safeImported->credentialRef.empty(), "imported credential_ref injection must be ignored");
        require(!importedDb.findAccount("../../escape").has_value(), "path-like account id must be rejected");
        require(!importedDb.findAccount("unknown-provider").has_value(), "unsupported provider must be rejected");
        require(!importedRouting.findGroup("../../unsafe-group").has_value(), "unsafe routing group id must be rejected");

        routerai::RoutingGroup manualGroup;
        manualGroup.id = "manual-cleanup";
        manualGroup.displayName = "Manual cleanup";
        manualGroup.strategy = routerai::RoutingStrategy::Manual;
        manualGroup.accountIds = {account.id};
        manualGroup.manualAccountId = account.id;
        importedRouting.saveGroup(manualGroup);

        importedDb.deleteAccount(account.id);
        require(!importedDb.findAccount(account.id).has_value(), "account delete failed");
        const auto cleanedGroup = importedRouting.findGroup(manualGroup.id);
        require(cleanedGroup.has_value(), "manual group should remain after account removal");
        require(cleanedGroup->accountIds.empty(), "removed account must leave routing membership");
        require(cleanedGroup->manualAccountId.empty(), "removed account must clear manual account selection");

        routerai::SQLiteDatabase maintenanceDb((root / "maintenance.db").string());
        maintenanceDb.initialize();

        routerai::Account maintenanceAccount;
        maintenanceAccount.id = "maint-zai";
        maintenanceAccount.provider = "zai";
        maintenanceAccount.providerMode = "general-api";
        maintenanceAccount.displayName = "Maintenance Z.ai";
        maintenanceAccount.credentialRef = "routerai-maintenance-test-missing-credential-91f5";
        maintenanceAccount.status = routerai::AccountStatus::Ready;
        maintenanceDb.insertAccount(maintenanceAccount);

        routerai::RoutingGroup brokenGroup;
        brokenGroup.id = "broken-manual";
        brokenGroup.displayName = "Broken manual";
        brokenGroup.strategy = routerai::RoutingStrategy::Manual;
        brokenGroup.accountIds = {maintenanceAccount.id, "ghost-account"};
        brokenGroup.manualAccountId = "ghost-account";
        maintenanceDb.saveRoutingGroup(brokenGroup);

        const auto runtimeRoot = root / "accounts";
        std::filesystem::create_directories(runtimeRoot / "orphan-account");
        routerai::CredentialStore maintenanceCredentials(root / "secrets");
        routerai::RoutingManager maintenanceRouting(maintenanceDb);
        routerai::MaintenanceManager maintenance(
            maintenanceDb,
            maintenanceRouting,
            maintenanceCredentials,
            runtimeRoot);

        const auto reportBefore = maintenance.inspect();
        require(reportBefore.missingCredentialRefs == 1, "missing credential reference should be reported");
        require(reportBefore.invalidRoutingMembers == 2, "invalid routing member/manual selection should be reported");
        require(reportBefore.orphanRuntimeDirectories == 1, "orphan runtime directory should be reported");

        const auto routingFixes = maintenance.repairRoutingGroups();
        require(routingFixes == 2, "maintenance should repair both invalid routing references");
        require(maintenance.inspect().invalidRoutingMembers == 0, "routing repair left invalid references");

        const auto removedRuntime = maintenance.removeOrphanRuntimeDirectories();
        require(removedRuntime == 1, "orphan runtime directory was not removed");
        require(!std::filesystem::exists(runtimeRoot / "orphan-account"), "orphan runtime directory still exists");

        for (int index = 0; index < 5; ++index) {
            routerai::RequestLogEntry retainedLog;
            retainedLog.groupId = "zai-default";
            retainedLog.accountId = maintenanceAccount.id;
            retainedLog.provider = "zai";
            retainedLog.model = "glm-5.2";
            retainedLog.statusCode = 200;
            retainedLog.durationMs = index;
            retainedLog.success = true;
            maintenanceDb.recordRequestLog(retainedLog);
        }
        require(maintenanceDb.countRequestLogs() == 5, "request log count mismatch before retention");
        routerai::RequestLogRetentionPolicy retention;
        retention.maxRows = 3;
        retention.maxAgeDays = 0;
        const auto retentionResult = maintenance.pruneRequestLogs(retention);
        require(retentionResult.beforeRows == 5, "retention before-row count mismatch");
        require(retentionResult.afterRows == 3, "retention row cap was not enforced");
        require(retentionResult.removedRows == 2, "retention removed-row count mismatch");
        require(maintenanceDb.listRequestLogs(10).size() == 3, "retention did not keep exactly three newest rows");

        sqlite3* rawDb = nullptr;
        require(sqlite3_open((root / "maintenance.db").string().c_str(), &rawDb) == SQLITE_OK,
            "failed to open maintenance database for age-retention setup");
        char* sqliteError = nullptr;
        const int ageSetup = sqlite3_exec(
            rawDb,
            "UPDATE request_logs SET created_at = datetime('now', '-45 days') "
            "WHERE id = (SELECT MIN(id) FROM request_logs);",
            nullptr,
            nullptr,
            &sqliteError);
        if (ageSetup != SQLITE_OK) {
            const std::string message = sqliteError ? sqliteError : "unknown sqlite error";
            sqlite3_free(sqliteError);
            sqlite3_close(rawDb);
            throw std::runtime_error("failed to prepare old request log: " + message);
        }
        sqlite3_close(rawDb);

        routerai::RequestLogRetentionPolicy ageRetention;
        ageRetention.maxRows = 100;
        ageRetention.maxAgeDays = 30;
        const auto ageResult = maintenance.pruneRequestLogs(ageRetention);
        require(ageResult.beforeRows == 3, "age retention before-row count mismatch");
        require(ageResult.afterRows == 2, "age retention did not remove the expired row");
        require(ageResult.removedRows == 1, "age retention removed-row count mismatch");

        std::filesystem::remove_all(root, ignored);
        std::cout << "ManagementFeaturesTests: OK\n";
    } catch (const std::exception& exception) {
        std::filesystem::remove_all(root, ignored);
        std::cerr << "ManagementFeaturesTests: FAILED: " << exception.what() << '\n';
        return 1;
    }
    return 0;
}
