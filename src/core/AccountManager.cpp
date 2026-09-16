#include "core/AccountManager.hpp"

#include "providers/codex/CodexProvider.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace routerai {

AccountManager::AccountManager(SQLiteDatabase& database) : database_(database) {}

Account AccountManager::addCodexAccount() {
    CodexProvider provider;
    Account account = provider.createPlaceholderAccount(nextAccountId(provider.name()));
    database_.insertAccount(account);
    return account;
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
            "Login is not implemented for provider: " + account->provider);
    }

    CodexProvider provider;
    const LoginResult result = provider.login(
        *account,
        LoginOptions{useBrowser});

    database_.updateAccount(*account);
    return AccountLoginOutcome{*account, result};
}

AccountAuthOutcome AccountManager::refreshAccountStatus(const std::string& accountId) {
    auto account = database_.findAccount(accountId);
    if (!account) {
        throw std::runtime_error("Account not found: " + accountId);
    }

    if (account->provider != "codex") {
        throw std::runtime_error(
            "Status refresh is not implemented for provider: " + account->provider);
    }

    CodexProvider provider;
    const AuthStatus auth = provider.authStatus(*account);

    if (auth.authenticated) {
        account->status = AccountStatus::Ready;
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
        if (account.provider == "codex") {
            refreshAccountStatus(account.id);
        }
    }
}

QuotaSnapshot AccountManager::readQuota(const std::string& accountId) const {
    const auto account = database_.findAccount(accountId);
    if (!account) {
        throw std::runtime_error("Account not found: " + accountId);
    }

    if (account->provider != "codex") {
        throw std::runtime_error(
            "Quota reads are not implemented for provider: " + account->provider);
    }

    CodexProvider provider;
    return provider.readQuota(*account);
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
