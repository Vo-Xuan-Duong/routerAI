#include "core/AccountManager.hpp"

#include <filesystem>
#include <stdexcept>

namespace routerai {

Account AccountManager::setAccountEnabled(const std::string& accountId, bool enabled) {
    auto account = database_.findAccount(accountId);
    if (!account) {
        throw std::runtime_error("Account not found: " + accountId);
    }

    // enabled is an operator-controlled routing switch. Keep auth/quota health
    // intact so re-enabling an account does not destroy its last known state.
    account->enabled = enabled;
    database_.updateAccount(*account);
    return *account;
}

Account AccountManager::setAccountPriority(const std::string& accountId, int priority) {
    if (priority < -100000 || priority > 100000) {
        throw std::runtime_error("Account priority must be between -100000 and 100000");
    }

    auto account = database_.findAccount(accountId);
    if (!account) {
        throw std::runtime_error("Account not found: " + accountId);
    }

    account->priority = priority;
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
