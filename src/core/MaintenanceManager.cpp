#include "core/MaintenanceManager.hpp"

#include "core/Account.hpp"
#include "core/RoutingManager.hpp"
#include "security/CredentialStore.hpp"
#include "storage/SQLiteDatabase.hpp"

#include <algorithm>
#include <system_error>
#include <unordered_set>
#include <utility>

namespace routerai {
namespace {

std::uintmax_t fileSizeIfPresent(const std::filesystem::path& path) {
    std::error_code error;
    if (!std::filesystem::exists(path, error)) return 0;
    const auto size = std::filesystem::file_size(path, error);
    return error ? 0 : size;
}

std::unordered_set<std::string> accountIds(const std::vector<Account>& accounts) {
    std::unordered_set<std::string> ids;
    ids.reserve(accounts.size());
    for (const auto& account : accounts) ids.insert(account.id);
    return ids;
}

}  // namespace

MaintenanceManager::MaintenanceManager(
    SQLiteDatabase& database,
    RoutingManager& routing,
    const CredentialStore& credentials,
    std::filesystem::path runtimeAccountsRoot)
    : database_(database),
      routing_(routing),
      credentials_(credentials),
      runtimeAccountsRoot_(std::move(runtimeAccountsRoot)) {}

MaintenanceReport MaintenanceManager::inspect() const {
    MaintenanceReport report;
    const std::filesystem::path databasePath(database_.path());
    report.databaseBytes =
        fileSizeIfPresent(databasePath) +
        fileSizeIfPresent(databasePath.string() + "-wal") +
        fileSizeIfPresent(databasePath.string() + "-shm");
    report.requestLogRows = database_.countRequestLogs();

    const auto accounts = database_.listAccounts();
    const auto knownAccounts = accountIds(accounts);

    for (const auto& account : accounts) {
        if (!account.credentialRef.empty() && !credentials_.exists(account.credentialRef)) {
            ++report.missingCredentialRefs;
            report.missingCredentialAccounts.push_back(account.id);
        }

        if (account.provider == "codex" && !account.runtimeHome.empty()) {
            std::error_code error;
            if (!std::filesystem::exists(account.runtimeHome, error)) {
                ++report.missingRuntimeDirectories;
                report.missingRuntimeAccounts.push_back(account.id);
            }
        }
    }

    for (const auto& group : routing_.listGroups()) {
        for (const auto& member : group.accountIds) {
            if (!knownAccounts.contains(member)) {
                ++report.invalidRoutingMembers;
                report.invalidRoutingEntries.push_back(group.id + ":" + member);
            }
        }
        if (!group.manualAccountId.empty() && !knownAccounts.contains(group.manualAccountId)) {
            ++report.invalidRoutingMembers;
            report.invalidRoutingEntries.push_back(
                group.id + ":manual=" + group.manualAccountId);
        }
    }

    std::error_code rootError;
    if (std::filesystem::exists(runtimeAccountsRoot_, rootError)) {
        std::filesystem::directory_iterator iterator(runtimeAccountsRoot_, rootError);
        const std::filesystem::directory_iterator end;
        while (!rootError && iterator != end) {
            const auto path = iterator->path();
            std::error_code typeError;
            const bool candidate =
                iterator->is_directory(typeError) || iterator->is_symlink(typeError);
            if (!typeError && candidate) {
                const std::string id = path.filename().string();
                if (!knownAccounts.contains(id)) {
                    ++report.orphanRuntimeDirectories;
                    report.orphanRuntimePaths.push_back(path);
                }
            }
            iterator.increment(rootError);
        }
    }

    return report;
}

RequestLogMaintenanceResult MaintenanceManager::pruneRequestLogs(
    const RequestLogRetentionPolicy& policy) {
    return database_.pruneRequestLogs(policy);
}

std::size_t MaintenanceManager::repairRoutingGroups() {
    const auto accounts = database_.listAccounts();
    const auto knownAccounts = accountIds(accounts);
    std::size_t fixes = 0;

    for (auto group : routing_.listGroups()) {
        bool changed = false;
        const auto before = group.accountIds.size();
        group.accountIds.erase(
            std::remove_if(
                group.accountIds.begin(),
                group.accountIds.end(),
                [&](const std::string& id) { return !knownAccounts.contains(id); }),
            group.accountIds.end());
        const auto removedMembers = before - group.accountIds.size();
        if (removedMembers > 0) {
            fixes += removedMembers;
            changed = true;
        }

        if (!group.manualAccountId.empty() && !knownAccounts.contains(group.manualAccountId)) {
            group.manualAccountId.clear();
            ++fixes;
            changed = true;
        }

        if (changed) routing_.saveGroup(group);
    }

    routing_.syncDefaultGroups();
    return fixes;
}

std::size_t MaintenanceManager::removeOrphanRuntimeDirectories() {
    const auto report = inspect();
    std::size_t removed = 0;
    for (const auto& path : report.orphanRuntimePaths) {
        std::error_code error;
        std::filesystem::remove_all(path, error);
        if (!error) ++removed;
    }
    return removed;
}

}  // namespace routerai
