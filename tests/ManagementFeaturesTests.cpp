#include "core/ConfigManager.hpp"
#include "core/RequestLog.hpp"
#include "core/RoutingManager.hpp"
#include "storage/SQLiteDatabase.hpp"

#include <filesystem>
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

        std::filesystem::remove_all(root, ignored);
        std::cout << "ManagementFeaturesTests: OK\n";
    } catch (const std::exception& exception) {
        std::filesystem::remove_all(root, ignored);
        std::cerr << "ManagementFeaturesTests: FAILED: " << exception.what() << '\n';
        return 1;
    }
    return 0;
}
