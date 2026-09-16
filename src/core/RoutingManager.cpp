#include "core/RoutingManager.hpp"

#include "core/FailurePolicy.hpp"

#include <algorithm>
#include <ctime>
#include <stdexcept>

namespace routerai {

namespace {

std::optional<double> latestUsedPercent(
    const std::vector<QuotaHistoryEntry>& history) {
    if (history.empty()) {
        return std::nullopt;
    }

    const std::int64_t latestSnapshotId = history.front().snapshotId;
    std::optional<double> highest;
    for (const auto& entry : history) {
        if (entry.snapshotId != latestSnapshotId) {
            break;
        }
        if (!highest || entry.usedPercent > *highest) {
            highest = entry.usedPercent;
        }
    }
    return highest;
}

bool eligible(const Account& account, std::int64_t nowUnix) {
    if (!account.enabled || FailurePolicy::isCoolingDown(account, nowUnix)) {
        return false;
    }
    return account.status == AccountStatus::Ready ||
           account.status == AccountStatus::Warning;
}

bool excluded(
    const std::string& accountId,
    const std::vector<std::string>& excludedAccountIds) {
    return std::find(
               excludedAccountIds.begin(),
               excludedAccountIds.end(),
               accountId) != excludedAccountIds.end();
}

int statusRank(AccountStatus status) {
    return status == AccountStatus::Ready ? 0 : 1;
}

std::optional<RoutingCandidate> pickHealthFirst(
    std::vector<RoutingCandidate> candidates,
    std::int64_t nowUnix) {
    candidates.erase(
        std::remove_if(
            candidates.begin(),
            candidates.end(),
            [&](const RoutingCandidate& candidate) {
                return !eligible(candidate.account, nowUnix);
            }),
        candidates.end());

    if (candidates.empty()) {
        return std::nullopt;
    }

    std::sort(
        candidates.begin(),
        candidates.end(),
        [](const RoutingCandidate& left, const RoutingCandidate& right) {
            const int leftStatus = statusRank(left.account.status);
            const int rightStatus = statusRank(right.account.status);
            if (leftStatus != rightStatus) {
                return leftStatus < rightStatus;
            }
            if (left.account.priority != right.account.priority) {
                return left.account.priority > right.account.priority;
            }
            const bool leftKnown = left.latestUsedPercent.has_value();
            const bool rightKnown = right.latestUsedPercent.has_value();
            if (leftKnown != rightKnown) {
                return leftKnown;
            }
            if (leftKnown && rightKnown && *left.latestUsedPercent != *right.latestUsedPercent) {
                return *left.latestUsedPercent < *right.latestUsedPercent;
            }
            return left.account.id < right.account.id;
        });
    return candidates.front();
}

std::optional<RoutingCandidate> pickPriority(
    std::vector<RoutingCandidate> candidates,
    std::int64_t nowUnix) {
    candidates.erase(
        std::remove_if(
            candidates.begin(),
            candidates.end(),
            [&](const RoutingCandidate& candidate) {
                return !eligible(candidate.account, nowUnix);
            }),
        candidates.end());
    if (candidates.empty()) {
        return std::nullopt;
    }

    std::sort(
        candidates.begin(),
        candidates.end(),
        [](const RoutingCandidate& left, const RoutingCandidate& right) {
            if (left.account.priority != right.account.priority) {
                return left.account.priority > right.account.priority;
            }
            const int leftStatus = statusRank(left.account.status);
            const int rightStatus = statusRank(right.account.status);
            if (leftStatus != rightStatus) {
                return leftStatus < rightStatus;
            }
            return left.account.id < right.account.id;
        });
    return candidates.front();
}

}  // namespace

RoutingManager::RoutingManager(SQLiteDatabase& database) : database_(database) {}

void RoutingManager::saveGroup(const RoutingGroup& group) {
    if (group.id.empty()) {
        throw std::runtime_error("Routing group id cannot be empty");
    }
    if (group.displayName.empty()) {
        throw std::runtime_error("Routing group display name cannot be empty");
    }
    database_.saveRoutingGroup(group);
}

std::optional<RoutingGroup> RoutingManager::findGroup(const std::string& groupId) const {
    return database_.findRoutingGroup(groupId);
}

std::vector<RoutingGroup> RoutingManager::listGroups() const {
    return database_.listRoutingGroups();
}

void RoutingManager::syncDefaultGroups() {
    const auto accounts = database_.listAccounts();

    RoutingGroup codex;
    codex.id = "codex-default";
    codex.displayName = "Codex manual";
    codex.strategy = RoutingStrategy::Manual;

    RoutingGroup antigravity;
    antigravity.id = "antigravity-default";
    antigravity.displayName = "Antigravity manual";
    antigravity.strategy = RoutingStrategy::Manual;

    RoutingGroup zai;
    zai.id = "zai-default";
    zai.displayName = "Z.ai default";
    zai.strategy = RoutingStrategy::HealthFirst;

    RoutingGroup mixed;
    mixed.id = "mixed-default";
    mixed.displayName = "Mixed API default";
    mixed.strategy = RoutingStrategy::HealthFirst;

    for (const auto& account : accounts) {
        if (account.provider == "codex") {
            // ChatGPT/Codex consumer profiles may be selected explicitly, but
            // are not automatically pooled into mixed/failover routing.
            codex.accountIds.push_back(account.id);
        } else if (account.provider == "antigravity") {
            antigravity.accountIds.push_back(account.id);
            if (account.providerMode == "api-project") {
                mixed.accountIds.push_back(account.id);
            }
        } else if (account.provider == "zai") {
            zai.accountIds.push_back(account.id);
            if (account.providerMode == "general-api") {
                mixed.accountIds.push_back(account.id);
            }
        }
    }

    const auto persist = [&](RoutingGroup& group, bool forceManual) {
        if (const auto existing = database_.findRoutingGroup(group.id)) {
            group.enabled = existing->enabled;
            group.manualAccountId = existing->manualAccountId;
            group.lastIndex = existing->lastIndex;
            if (!forceManual) {
                group.strategy = existing->strategy;
            }
        }

        if (forceManual) {
            group.strategy = RoutingStrategy::Manual;
            if (!group.manualAccountId.empty() &&
                std::find(group.accountIds.begin(), group.accountIds.end(), group.manualAccountId) == group.accountIds.end()) {
                group.manualAccountId.clear();
            }
        }

        database_.saveRoutingGroup(group);
    };

    persist(codex, true);
    persist(antigravity, true);
    persist(zai, false);
    persist(mixed, false);
}

std::vector<RoutingCandidate> RoutingManager::candidatesFor(const RoutingGroup& group) const {
    std::vector<RoutingCandidate> candidates;
    candidates.reserve(group.accountIds.size());
    for (const auto& accountId : group.accountIds) {
        const auto account = database_.findAccount(accountId);
        if (!account) {
            continue;
        }
        RoutingCandidate candidate;
        candidate.account = *account;
        candidate.latestUsedPercent = latestUsedPercent(
            database_.listQuotaHistory(accountId, 100));
        candidates.push_back(std::move(candidate));
    }
    return candidates;
}

std::optional<RoutingDecision> RoutingManager::select(
    const std::string& groupId,
    std::int64_t nowUnix,
    const std::vector<std::string>& excludedAccountIds) {
    auto group = database_.findRoutingGroup(groupId);
    if (!group || !group->enabled) {
        return std::nullopt;
    }

    auto candidates = candidatesFor(*group);
    candidates.erase(
        std::remove_if(
            candidates.begin(),
            candidates.end(),
            [&](const RoutingCandidate& candidate) {
                return excluded(candidate.account.id, excludedAccountIds);
            }),
        candidates.end());

    std::optional<RoutingCandidate> selected;

    switch (group->strategy) {
        case RoutingStrategy::HealthFirst:
            selected = pickHealthFirst(candidates, nowUnix);
            break;
        case RoutingStrategy::LeastUsed: {
            AccountSelector selector;
            selected = selector.select(candidates, nowUnix);
            break;
        }
        case RoutingStrategy::Priority:
            selected = pickPriority(candidates, nowUnix);
            break;
        case RoutingStrategy::Manual:
            for (const auto& candidate : candidates) {
                if (candidate.account.id == group->manualAccountId &&
                    eligible(candidate.account, nowUnix)) {
                    selected = candidate;
                    break;
                }
            }
            break;
        case RoutingStrategy::RoundRobin: {
            if (!group->accountIds.empty()) {
                const int count = static_cast<int>(group->accountIds.size());
                const int start = (group->lastIndex + 1 + count) % count;
                for (int offset = 0; offset < count; ++offset) {
                    const int index = (start + offset) % count;
                    const auto& wantedId = group->accountIds[static_cast<std::size_t>(index)];
                    if (excluded(wantedId, excludedAccountIds)) {
                        continue;
                    }
                    const auto it = std::find_if(
                        candidates.begin(),
                        candidates.end(),
                        [&](const RoutingCandidate& candidate) {
                            return candidate.account.id == wantedId &&
                                   eligible(candidate.account, nowUnix);
                        });
                    if (it != candidates.end()) {
                        selected = *it;
                        group->lastIndex = index;
                        database_.updateRoutingGroupCursor(group->id, index);
                        break;
                    }
                }
            }
            break;
        }
    }

    if (!selected) {
        return std::nullopt;
    }
    return RoutingDecision{*group, *selected};
}

std::optional<RoutingDecision> RoutingManager::select(
    const std::string& groupId,
    std::int64_t nowUnix) {
    return select(groupId, nowUnix, {});
}

std::optional<RoutingDecision> RoutingManager::select(
    const std::string& groupId,
    const std::vector<std::string>& excludedAccountIds) {
    return select(
        groupId,
        static_cast<std::int64_t>(std::time(nullptr)),
        excludedAccountIds);
}

std::optional<RoutingDecision> RoutingManager::select(const std::string& groupId) {
    return select(groupId, static_cast<std::int64_t>(std::time(nullptr)), {});
}

void RoutingManager::recordFailure(
    const std::string& accountId,
    const std::string& error,
    std::int64_t nowUnix) {
    auto account = database_.findAccount(accountId);
    if (!account) {
        throw std::runtime_error("Account not found: " + accountId);
    }
    FailurePolicy::recordFailure(*account, nowUnix, error);
    database_.updateAccount(*account);
}

void RoutingManager::recordSuccess(const std::string& accountId) {
    auto account = database_.findAccount(accountId);
    if (!account) {
        throw std::runtime_error("Account not found: " + accountId);
    }
    FailurePolicy::recordSuccess(*account);
    database_.updateAccount(*account);
}

}  // namespace routerai
