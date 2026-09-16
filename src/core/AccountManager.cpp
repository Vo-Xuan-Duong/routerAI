#include "core/AccountManager.hpp"

#include "providers/codex/CodexProvider.hpp"

#include <iomanip>
#include <sstream>

namespace routerai {

AccountManager::AccountManager(SQLiteDatabase& database) : database_(database) {}

Account AccountManager::addCodexPlaceholder() {
    CodexProvider provider;
    Account account = provider.createPlaceholderAccount(nextAccountId(provider.name()));
    database_.insertAccount(account);
    return account;
}

std::vector<Account> AccountManager::listAccounts() const {
    return database_.listAccounts();
}

std::size_t AccountManager::countAccounts() const {
    return database_.countAccounts();
}

std::string AccountManager::nextAccountId(const std::string& provider) const {
    const auto accounts = database_.listAccounts();
    std::size_t count = 0;
    for (const auto& account : accounts) {
        if (account.provider == provider) {
            ++count;
        }
    }

    std::ostringstream id;
    id << provider << '-' << std::setw(2) << std::setfill('0') << (count + 1);
    return id.str();
}

}  // namespace routerai
