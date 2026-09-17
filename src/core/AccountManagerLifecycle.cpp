#include "core/AccountManager.hpp"

#include <filesystem>
#include <stdexcept>

namespace routerai {

Account AccountManager::setAccountEnabled(const std::string& accountId, bool enabled) {
    auto account = database_.findAccount(accountId);
    if (!account) {
        throw std::runtime_error("Account not found: " + accountId);
    }

    account->enabled = enabled;
    if (!enabled) {
        account->status = AccountStatus::Disabled;
    } else if (account->status == AccountStatus::Disabled) {
        account->status = AccountStatus::AuthExpired;
    }
    database_.updateAccount(*account);
    return *account;
}

void AccountManager::removeAccount(const std::string& accountId) {
    const auto account = database_.findAccount(accountId);
    if (!account) {
        throw std::runtime_error("Account not found: " + accountId);
    }

    if (!account->credentialRef.empty()) {
        credentials_.erase(account->credentialRef);
    }

    database_.deleteAccount(accountId);

    if (!account->runtimeHome.empty()) {
        std::error_code ignored;
        std::filesystem::remove_all(account->runtimeHome, ignored);
    }
}

}  // namespace routerai
