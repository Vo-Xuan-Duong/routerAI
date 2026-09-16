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

        database.insertAccount(account("codex-01", "codex", "chatgpt"));
        database.insertAccount(account("antigravity-01", "antigravity", "consumer-cli"));
        database.insertAccount(account("zai-coding-01", "zai", "coding-plan"));
        database.insertAccount(account("antigravity-api-01", "antigravity", "api-project"));
        routing.syncDefaultGroups();

        const auto codex = database.findRoutingGroup("codex-default");
        const auto antigravity = database.findRoutingGroup("antigravity-default");
        const auto mixed = database.findRoutingGroup("mixed-default");
        require(codex.has_value(), "codex default group missing");
        require(antigravity.has_value(), "antigravity default group missing");
        require(mixed.has_value(), "mixed default group missing");
        require(codex->strategy == routerai::RoutingStrategy::Manual, "Codex consumer routing must stay manual");
        require(antigravity->strategy == routerai::RoutingStrategy::Manual, "Antigravity consumer routing must stay manual");
        require(!contains(mixed->accountIds, "codex-01"), "Codex consumer account must not enter mixed API pool");
        require(!contains(mixed->accountIds, "antigravity-01"), "Antigravity consumer account must not enter mixed API pool");
        require(!contains(mixed->accountIds, "zai-coding-01"), "Z.ai Coding Plan must not enter general mixed API pool");
        require(contains(mixed->accountIds, "zai-01"), "Z.ai General API account should enter mixed API pool");
        require(contains(mixed->accountIds, "antigravity-api-01"), "Antigravity API project should enter mixed API pool");

        std::cout << "RoutingManagerTests: OK\n";
    } catch (const std::exception& exception) {
        std::cerr << "RoutingManagerTests: FAILED: " << exception.what() << '\n';
        std::filesystem::remove(path, ignored);
        return 1;
    }

    std::filesystem::remove(path, ignored);
    return 0;
}
