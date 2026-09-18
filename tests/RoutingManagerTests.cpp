#include "core/RoutingManager.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

routerai::Account account(
    const std::string& id,
    const std::string& provider = "zai",
    const std::string& mode = "general-api",
    int priority = 100) {
    routerai::Account value;
    value.id = id;
    value.provider = provider;
    value.providerMode = mode;
    value.displayName = id;
    value.status = routerai::AccountStatus::Ready;
    value.priority = priority;
    value.enabled = true;
    return value;
}

bool contains(const std::vector<std::string>& values, const std::string& value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

}  // namespace

int main() {
    const std::filesystem::path path = "routing-manager-test.db";
    std::error_code ignored;
    std::filesystem::remove(path, ignored);

    try {
        routerai::SQLiteDatabase database(path.string());
        database.initialize();
        database.insertAccount(account("zai-01"));
        database.insertAccount(account("zai-02"));
        database.insertAccount(account("zai-03"));

        routerai::RoutingManager routing(database);

        routerai::RoutingGroup roundRobin;
        roundRobin.id = "rr";
        roundRobin.displayName = "Round robin";
        roundRobin.strategy = routerai::RoutingStrategy::RoundRobin;
        roundRobin.accountIds = {"zai-01", "zai-02", "zai-03"};
        routing.saveGroup(roundRobin);

        const auto first = routing.select("rr", 1000);
        const auto second = routing.select("rr", 1000);
        const auto third = routing.select("rr", 1000);
        require(first && first->candidate.account.id == "zai-01", "round robin first account mismatch");
        require(second && second->candidate.account.id == "zai-02", "round robin second account mismatch");
        require(third && third->candidate.account.id == "zai-03", "round robin third account mismatch");

        routerai::RoutingGroup failover;
        failover.id = "failover";
        failover.displayName = "Failover";
        failover.strategy = routerai::RoutingStrategy::Priority;
        failover.accountIds = {"zai-01", "zai-02"};
        routing.saveGroup(failover);

        const auto beforeFailure = routing.select("failover", 2000);
        require(beforeFailure && beforeFailure->candidate.account.id == "zai-01", "expected deterministic first account");

        const auto excludeFirst = routing.select("failover", 2000, {"zai-01"});
        require(excludeFirst && excludeFirst->candidate.account.id == "zai-02", "request-local exclusion must select the next account");
        const auto excludeAll = routing.select("failover", 2000, {"zai-01", "zai-02"});
        require(!excludeAll.has_value(), "excluding every group member must produce no selection");

        routing.recordFailure("zai-01", "test failure", 2000);
        const auto duringCooldown = routing.select("failover", 2001);
        require(duringCooldown && duringCooldown->candidate.account.id == "zai-02", "cooldown account must be skipped");

        routing.recordSuccess("zai-01");
        const auto afterSuccess = routing.select("failover", 2001);
        require(afterSuccess && afterSuccess->candidate.account.id == "zai-01", "successful account should re-enter routing");

        const auto persisted = database.findRoutingGroup("rr");
        require(persisted.has_value(), "routing group must persist");
        require(persisted->lastIndex == 2, "round robin cursor must persist");
        require(persisted->accountIds.size() == 3, "routing group members must persist");

        const auto previewPrimary = routing.preview("rr", 1000);
        require(previewPrimary && previewPrimary->candidate.account.id == "zai-01",
                "round robin preview must expose the next candidate");
        const auto afterPrimaryPreview = database.findRoutingGroup("rr");
        require(afterPrimaryPreview && afterPrimaryPreview->lastIndex == 2,
                "routing preview must not advance the round robin cursor");

        const auto previewBackup = routing.preview("rr", 1000, {"zai-01"});
        require(previewBackup && previewBackup->candidate.account.id == "zai-02",
                "routing preview exclusion must expose the next failover candidate");
        const auto afterBackupPreview = database.findRoutingGroup("rr");
        require(afterBackupPreview && afterBackupPreview->lastIndex == 2,
                "failover preview must not mutate the round robin cursor");

        database.insertAccount(account("codex-01", "codex", "chatgpt"));
        database.insertAccount(account("antigravity-01", "antigravity", "consumer-cli"));
        database.insertAccount(account("zai-coding-01", "zai", "coding-plan"));
        database.insertAccount(account("antigravity-api-01", "antigravity", "api-project"));
        routing.syncDefaultGroups();

        const auto codex = database.findRoutingGroup("codex-default");
        const auto antigravityConsumer = database.findRoutingGroup("antigravity-default");
        const auto antigravityApi = database.findRoutingGroup("antigravity-api-default");
        const auto zai = database.findRoutingGroup("zai-default");
        const auto mixed = database.findRoutingGroup("mixed-default");
        require(codex.has_value(), "codex default group missing");
        require(antigravityConsumer.has_value(), "antigravity consumer group missing");
        require(antigravityApi.has_value(), "antigravity API group missing");
        require(zai.has_value(), "Z.ai default group missing");
        require(mixed.has_value(), "mixed default group missing");

        require(codex->strategy == routerai::RoutingStrategy::Manual, "Codex consumer routing must stay manual");
        require(antigravityConsumer->strategy == routerai::RoutingStrategy::Manual, "Antigravity consumer routing must stay manual");
        require(contains(codex->accountIds, "codex-01"), "Codex consumer must be available for explicit manual selection");
        require(contains(antigravityConsumer->accountIds, "antigravity-01"), "Antigravity consumer must stay in the manual consumer group");
        require(!contains(antigravityConsumer->accountIds, "antigravity-api-01"), "Antigravity API project must not be mixed into consumer group");
        require(contains(antigravityApi->accountIds, "antigravity-api-01"), "Antigravity API project must enter its API pool");
        require(!contains(antigravityApi->accountIds, "antigravity-01"), "Consumer session must not enter Antigravity API pool");
        require(contains(zai->accountIds, "zai-01"), "Z.ai General API must enter Z.ai automatic pool");
        require(!contains(zai->accountIds, "zai-coding-01"), "Z.ai Coding Plan must not enter General API automatic pool");

        require(!contains(mixed->accountIds, "codex-01"), "Codex consumer account must not enter mixed API pool");
        require(!contains(mixed->accountIds, "antigravity-01"), "Antigravity consumer account must not enter mixed API pool");
        require(!contains(mixed->accountIds, "zai-coding-01"), "Z.ai Coding Plan must not enter general mixed API pool");
        require(contains(mixed->accountIds, "zai-01"), "Z.ai General API account should enter mixed API pool");
        require(contains(mixed->accountIds, "antigravity-api-01"), "Antigravity API project should enter mixed API pool");

        require(!routing.groupSupportsCompletions(*codex), "manual Codex group without a selected account must not be advertised");
        routerai::RoutingGroup selectedCodex = *codex;
        selectedCodex.manualAccountId = "codex-01";
        routing.saveGroup(selectedCodex);
        require(routing.groupSupportsCompletions(selectedCodex), "manual Codex group should be executable after selecting an account");
        require(!routing.groupSupportsCompletions(*antigravityConsumer), "Antigravity consumer quota/profile group must not be advertised as a completion backend");
        require(routing.groupSupportsCompletions(*antigravityApi), "Antigravity API pool must be executable");
        require(routing.groupSupportsCompletions(*zai), "Z.ai API pool must be executable");

        routerai::RoutingGroup invalidAutomatic;
        invalidAutomatic.id = "invalid-consumer-auto";
        invalidAutomatic.displayName = "Invalid consumer auto pool";
        invalidAutomatic.strategy = routerai::RoutingStrategy::RoundRobin;
        invalidAutomatic.accountIds = {"codex-01"};
        bool rejectedConsumerAuto = false;
        try {
            routing.saveGroup(invalidAutomatic);
        } catch (const std::exception&) {
            rejectedConsumerAuto = true;
        }
        require(rejectedConsumerAuto, "automatic custom groups must reject consumer-only accounts");

        // Simulate an old/external SQLite row that bypassed RoutingManager::saveGroup.
        // Selection itself must still refuse to auto-cycle the consumer profile.
        routerai::RoutingGroup legacyAutomatic;
        legacyAutomatic.id = "legacy-consumer-auto";
        legacyAutomatic.displayName = "Legacy consumer auto pool";
        legacyAutomatic.strategy = routerai::RoutingStrategy::RoundRobin;
        legacyAutomatic.accountIds = {"codex-01", "zai-01"};
        database.saveRoutingGroup(legacyAutomatic);
        const auto legacySelection = routing.select("legacy-consumer-auto", 3000);
        require(legacySelection.has_value(), "legacy automatic group should still find API-capable members");
        require(legacySelection->candidate.account.id == "zai-01", "legacy automatic group must skip consumer accounts at selection time");

        routerai::RoutingGroup validMixedApi;
        validMixedApi.id = "custom-api";
        validMixedApi.displayName = "Custom API";
        validMixedApi.strategy = routerai::RoutingStrategy::RoundRobin;
        validMixedApi.accountIds = {"zai-01", "antigravity-api-01"};
        routing.saveGroup(validMixedApi);
        require(database.findRoutingGroup("custom-api").has_value(), "automatic custom API group should persist");

        std::cout << "RoutingManagerTests: OK\n";
    } catch (const std::exception& exception) {
        std::cerr << "RoutingManagerTests: FAILED: " << exception.what() << '\n';
        std::filesystem::remove(path, ignored);
        return 1;
    }

    std::filesystem::remove(path, ignored);
    return 0;
}
