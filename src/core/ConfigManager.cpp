#include "core/ConfigManager.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace routerai {
namespace {

RoutingStrategy parseStrategy(const std::string& value) {
    if (value == "LEAST_USED") return RoutingStrategy::LeastUsed;
    if (value == "PRIORITY") return RoutingStrategy::Priority;
    if (value == "ROUND_ROBIN") return RoutingStrategy::RoundRobin;
    if (value == "MANUAL") return RoutingStrategy::Manual;
    return RoutingStrategy::HealthFirst;
}

std::string defaultRuntimeHome(const Account& account) {
    if (account.provider == "codex") {
        return (std::filesystem::path(".routerai") / "accounts" / account.id / "codex-home").string();
    }
    return {};
}

nlohmann::json buildExport(SQLiteDatabase& database, RoutingManager& routing) {
    nlohmann::json root = {
        {"schema_version", 1},
        {"accounts", nlohmann::json::array()},
        {"routing_groups", nlohmann::json::array()},
    };

    for (const auto& account : database.listAccounts()) {
        root["accounts"].push_back({
            {"id", account.id},
            {"provider", account.provider},
            {"provider_mode", account.providerMode},
            {"display_name", account.displayName},
            {"email", account.email},
            {"plan_type", account.planType},
            {"priority", account.priority},
            {"enabled", account.enabled},
        });
    }

    for (const auto& group : routing.listGroups()) {
        root["routing_groups"].push_back({
            {"id", group.id},
            {"display_name", group.displayName},
            {"strategy", toString(group.strategy)},
            {"enabled", group.enabled},
            {"manual_account_id", group.manualAccountId},
            {"account_ids", group.accountIds},
        });
    }
    return root;
}

ConfigTransferResult applyImport(
    SQLiteDatabase& database,
    RoutingManager& routing,
    const nlohmann::json& root) {
    if (!root.is_object() || root.value("schema_version", 0) != 1) {
        throw std::runtime_error("Unsupported routerAI config schema");
    }

    ConfigTransferResult result;
    const auto accountsJson = root.value("accounts", nlohmann::json::array());
    if (!accountsJson.is_array()) throw std::runtime_error("accounts must be an array");

    for (const auto& item : accountsJson) {
        if (!item.is_object()) continue;
        const std::string id = item.value("id", std::string{});
        const std::string provider = item.value("provider", std::string{});
        if (id.empty() || provider.empty()) continue;

        const auto existing = database.findAccount(id);
        Account account;
        if (existing) {
            account = *existing;
        } else {
            account.id = id;
            account.provider = provider;
            account.status = AccountStatus::AuthExpired;
            account.enabled = true;
        }

        account.provider = provider;
        account.providerMode = item.value("provider_mode", account.providerMode);
        account.displayName = item.value("display_name", account.displayName);
        account.email = item.value("email", account.email);
        account.planType = item.value("plan_type", account.planType);
        account.priority = item.value("priority", account.priority);
        account.enabled = item.value("enabled", account.enabled);
        if (account.runtimeHome.empty()) account.runtimeHome = defaultRuntimeHome(account);
        if (!account.enabled) account.status = AccountStatus::Disabled;
        else if (!existing) account.status = AccountStatus::AuthExpired;

        // credentialRef intentionally comes only from an existing local row.
        // It is never accepted from exported/imported JSON.
        if (existing) database.updateAccount(account);
        else database.insertAccount(account);
        ++result.accounts;
    }

    routing.syncDefaultGroups();
    const auto groupsJson = root.value("routing_groups", nlohmann::json::array());
    if (!groupsJson.is_array()) throw std::runtime_error("routing_groups must be an array");

    std::unordered_set<std::string> knownAccounts;
    for (const auto& account : database.listAccounts()) knownAccounts.insert(account.id);

    for (const auto& item : groupsJson) {
        if (!item.is_object()) continue;
        RoutingGroup group;
        group.id = item.value("id", std::string{});
        if (group.id.empty()) continue;
        group.displayName = item.value("display_name", group.id);
        group.strategy = parseStrategy(item.value("strategy", std::string("HEALTH_FIRST")));
        group.enabled = item.value("enabled", true);
        group.manualAccountId = item.value("manual_account_id", std::string{});
        if (!group.manualAccountId.empty() && !knownAccounts.contains(group.manualAccountId)) {
            group.manualAccountId.clear();
        }
        if (item.contains("account_ids") && item.at("account_ids").is_array()) {
            for (const auto& idValue : item.at("account_ids")) {
                if (!idValue.is_string()) continue;
                const std::string member = idValue.get<std::string>();
                if (knownAccounts.contains(member)) group.accountIds.push_back(member);
            }
        }
        routing.saveGroup(group);
        ++result.routingGroups;
    }

    routing.syncDefaultGroups();
    result.detail = "Imported metadata. Secrets were not present; reconfigure provider credentials where required.";
    return result;
}

}  // namespace

ConfigManager::ConfigManager(SQLiteDatabase& database, RoutingManager& routing)
    : database_(database), routing_(routing) {}

std::string ConfigManager::exportJson() const {
    return buildExport(database_, routing_).dump(2) + "\n";
}

ConfigTransferResult ConfigManager::importJson(const std::string& jsonText) {
    return applyImport(database_, routing_, nlohmann::json::parse(jsonText));
}

ConfigTransferResult ConfigManager::exportTo(const std::filesystem::path& path) const {
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("Cannot open config export path: " + path.string());
    const std::string jsonText = exportJson();
    output.write(jsonText.data(), static_cast<std::streamsize>(jsonText.size()));
    if (!output) throw std::runtime_error("Failed to write config export: " + path.string());

    ConfigTransferResult result;
    result.accounts = database_.listAccounts().size();
    result.routingGroups = routing_.listGroups().size();
    result.detail = "Exported metadata only. Provider secrets and credential references were excluded.";
    return result;
}

ConfigTransferResult ConfigManager::importFrom(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot open config import path: " + path.string());
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return importJson(buffer.str());
}

}  // namespace routerai
