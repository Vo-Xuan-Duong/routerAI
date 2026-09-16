#include "providers/codex/CodexProvider.hpp"

namespace routerai {

std::string CodexProvider::name() const {
    return "codex";
}

Account CodexProvider::createPlaceholderAccount(const std::string& accountId) const {
    Account account;
    account.id = accountId;
    account.provider = name();
    account.displayName = "Pending Codex OAuth";
    account.status = AccountStatus::AuthExpired;
    account.priority = 100;
    account.enabled = true;
    return account;
}

}  // namespace routerai
