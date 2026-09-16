#pragma once

#include "core/Account.hpp"
#include "core/Provider.hpp"
#include "core/Quota.hpp"
#include "storage/SQLiteDatabase.hpp"

#include <optional>
#include <string>
#include <vector>

namespace routerai {

struct AccountLoginOutcome {
    Account account;
    LoginResult result;
};

struct AccountAuthOutcome {
    Account account;
    AuthStatus auth;
};

class AccountManager {
public:
    explicit AccountManager(SQLiteDatabase& database);

    Account addCodexAccount();
    AccountLoginOutcome loginAccount(const std::string& accountId, bool useBrowser);
    AccountAuthOutcome refreshAccountStatus(const std::string& accountId);
    void refreshAllAccountStatuses();
    QuotaSnapshot readQuota(const std::string& accountId) const;

    std::optional<Account> findAccount(const std::string& accountId) const;
    std::vector<Account> listAccounts() const;
    std::size_t countAccounts() const;

private:
    SQLiteDatabase& database_;

    std::string nextAccountId(const std::string& provider) const;
};

}  // namespace routerai
