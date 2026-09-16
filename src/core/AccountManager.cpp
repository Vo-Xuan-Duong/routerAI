#include "core/AccountManager.hpp"

#include "providers/codex/CodexProvider.hpp"
#include "providers/zai/ZaiProvider.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace routerai {

namespace {

void applyProfile(Account& account, const AccountProfile& profile) {
    if (!profile.email.empty()) {
        account.email = profile.email;
        account.displayName = profile.email;
    }
    if (!profile.planType.empty()) {
        account.planType = profile.planType;
    }
}

void appendDetail(std::string& detail, const std::string& extra) {
    if (extra.empty()) {
        return;
    }
    if (!detail.empty()) {
        detail += " | ";
    }
    detail += extra;
}

std::optional<AccountStatus> statusFromQuota(const QuotaSnapshot& snapshot) {
    if (!snapshot.ordinaryUsageAllowed.has_value()) {
        return std::nullopt;
    }
    if (!*snapshot.ordinaryUsageAllowed) {
        return AccountStatus::Limited;
    }

    double highestUsedPercent = 0.0;
    bool reachedSignal = false;
    for (const auto& bucket : snapshot.buckets) {
        reachedSignal = reachedSignal || !bucket.reachedType.empty();
        for (const auto& window : bucket.windows) {
            highestUsedPercent = std::max(highestUsedPercent, window.usedPercent);
        }
    }

    if (reachedSignal || highestUsedPercent >= 90.0) {
        return AccountStatus::Warning;
    }
    return AccountStatus::Ready;
}

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

}  // namespace

AccountManager::AccountManager(SQLiteDatabase& database, CredentialStore& credentials)
    : database_(database), credentials_(credentials) {}

Account AccountManager::addCodexAccount() {
    CodexProvider provider;
    Account account = provider.createPlaceholderAccount(nextAccountId(provider.name()));
    database_.insertAccount(account);
    return account;
}

Account AccountManager::addZaiAccount(const std::string& mode) {
    ZaiProvider provider;
    Account account = provider.createAccount(nextAccountId(provider.name()), mode);
    database_.insertAccount(account);
    return account;
}

AccountAuthOutcome AccountManager::configureZaiApiKey(
    const std::string& accountId,
    const std::string& apiKey,
    const std::string& mode) {
    auto account = database_.findAccount(accountId);
    if (!account) {
        throw std::runtime_error("Account not found: " + accountId);
    }
    if (account->provider != "zai") {
        throw std::runtime_error("Account is not a Z.ai provider account: " + accountId);
    }
    if (mode != "general-api" && mode != "coding-plan") {
        throw std::runtime_error("Unsupported Z.ai account mode: " + mode);
    }

    const std::string reference = account->id + "-api-key";
    credentials_.put(reference, apiKey);
    account->credentialRef = reference;
    account->providerMode = mode;
    account->planType = mode;
    account->displayName = mode == "coding-plan"
        ? "Z.ai Coding Plan"
        : "Z.ai General API";
    account->status = AccountStatus::Ready;
    account->lastError.clear();
    account->consecutiveFailures = 0;
    account->cooldownUntilUnix.reset();
    database_.updateAccount(*account);

    const std::string detail = mode == "general-api"
        ? "Z.ai General API credential stored locally"
        : "Z.ai Coding Plan credential stored locally; unified routing is disabled for this mode";
    return AccountAuthOutcome{*account, AuthStatus{true, detail}};
}

AccountLoginOutcome AccountManager::loginAccount(
    const std::string& accountId,
    bool useBrowser) {
    auto account = database_.findAccount(accountId);
    if (!account) {
        throw std::runtime_error("Account not found: " + accountId);
    }

    if (account->provider != "codex") {
        throw std::runtime_error(
            "Interactive login is not implemented for provider: " + account->provider);
    }

    CodexProvider provider;
    LoginResult result = provider.login(
        *account,
        LoginOptions{useBrowser});

    if (result.success) {
        try {
            applyProfile(*account, provider.readProfile(*account));
        } catch (const std::exception& exception) {
            appendDetail(
                result.detail,
                "profile sync warning: " + std::string(exception.what()));
        }
    }

    database_.updateAccount(*account);
    return AccountLoginOutcome{*account, result};
}

AccountAuthOutcome AccountManager::refreshAccountStatus(const std::string& accountId) {
    auto account = database_.findAccount(accountId);
    if (!account) {
        throw std::runtime_error("Account not found: " + accountId);
    }

    if (account->provider == "zai") {
        const bool configured =
            !account->credentialRef.empty() && credentials_.exists(account->credentialRef);
        account->status = configured ? AccountStatus::Ready : AccountStatus::AuthExpired;
        database_.updateAccount(*account);
        return AccountAuthOutcome{
            *account,
            AuthStatus{
                configured,
                configured ? "Z.ai credential is available" : "Z.ai API key is not configured"}}
        ;
    }

    if (account->provider != "codex") {
        throw std::runtime_error(
            "Status refresh is not implemented for provider: " + account->provider);
    }

    CodexProvider provider;
    AuthStatus auth = provider.authStatus(*account);

    if (auth.authenticated) {
        if (account->status == AccountStatus::AuthExpired ||
            account->status == AccountStatus::Error) {
            account->status = AccountStatus::Ready;
        }
        try {
            applyProfile(*account, provider.readProfile(*account));
        } catch (const std::exception& exception) {
            appendDetail(
                auth.detail,
                "profile sync warning: " + std::string(exception.what()));
        }
    } else if (auth.detail.find("not installed") != std::string::npos) {
        account->status = AccountStatus::Error;
    } else {
        account->status = AccountStatus::AuthExpired;
    }

    database_.updateAccount(*account);
    return AccountAuthOutcome{*account, auth};
}

void AccountManager::refreshAllAccountStatuses() {
    const auto accounts = database_.listAccounts();
    for (const auto& account : accounts) {
        if (account.provider == "codex" || account.provider == "zai") {
            refreshAccountStatus(account.id);
        }
    }
}

QuotaSnapshot AccountManager::readQuota(const std::string& accountId) {
    const auto account = database_.findAccount(accountId);
    if (!account) {
        throw std::runtime_error("Account not found: " + accountId);
    }

    if (account->provider != "codex") {
        throw std::runtime_error(
            "Quota reads are not implemented for provider: " + account->provider);
    }

    CodexProvider provider;
    QuotaSnapshot snapshot = provider.readQuota(*account);
    database_.recordQuotaSnapshot(accountId, snapshot);

    if (const auto derivedStatus = statusFromQuota(snapshot)) {
        Account updated = *account;
        updated.status = *derivedStatus;
        database_.updateAccount(updated);
    }

    return snapshot;
}

std::vector<QuotaHistoryEntry> AccountManager::listQuotaHistory(
    const std::string& accountId,
    std::size_t limit) const {
    if (!database_.findAccount(accountId)) {
        throw std::runtime_error("Account not found: " + accountId);
    }
    return database_.listQuotaHistory(accountId, limit);
}

std::optional<RoutingCandidate> AccountManager::selectAccount(
    const std::string& provider) const {
    std::vector<RoutingCandidate> candidates;
    for (const auto& account : database_.listAccounts()) {
        if (account.provider != provider) {
            continue;
        }

        RoutingCandidate candidate;
        candidate.account = account;
        candidate.latestUsedPercent = latestUsedPercent(
            database_.listQuotaHistory(account.id, 100));
        candidates.push_back(std::move(candidate));
    }

    AccountSelector selector;
    return selector.select(candidates);
}

std::optional<Account> AccountManager::findAccount(const std::string& accountId) const {
    return database_.findAccount(accountId);
}

std::vector<Account> AccountManager::listAccounts() const {
    return database_.listAccounts();
}

std::size_t AccountManager::countAccounts() const {
    return database_.countAccounts();
}

std::string AccountManager::nextAccountId(const std::string& provider) const {
    const auto accounts = database_.listAccounts();
    std::size_t highest = 0;

    const std::string prefix = provider + '-';
    for (const auto& account : accounts) {
        if (!account.id.starts_with(prefix)) {
            continue;
        }

        try {
            const auto suffix = account.id.substr(prefix.size());
            highest = std::max(highest, static_cast<std::size_t>(std::stoul(suffix)));
        } catch (...) {
            // Ignore manually named accounts that do not use the numeric convention.
        }
    }

    std::ostringstream id;
    id << provider << '-' << std::setw(2) << std::setfill('0') << (highest + 1);
    return id.str();
}

}  // namespace routerai
