#pragma once

#include "core/Account.hpp"
#include "core/AccountSelector.hpp"
#include "core/Provider.hpp"
#include "core/Quota.hpp"
#include "security/CredentialStore.hpp"
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
    AccountManager(SQLiteDatabase& database, CredentialStore& credentials);

    Account addCodexAccount();
    Account addZaiAccount(const std::string& mode = "general-api");
    AccountAuthOutcome configureZaiApiKey(
        const std::string& accountId,
        const std::string& apiKey,
        const std::string& mode);

    AccountLoginOutcome loginAccount(const std::string& accountId, bool useBrowser);
    AccountAuthOutcome refreshAccountStatus(const std::string& accountId);
    void refreshAllAccountStatuses();
    QuotaSnapshot readQuota(const std::string& accountId);
    std::vector<QuotaHistoryEntry> listQuotaHistory(
        const std::string& accountId,
        std::size_t limit = 50) const;
    std::optional<RoutingCandidate> selectAccount(
        const std::string& provider = "codex") const;

    std::optional<Account> findAccount(const std::string& accountId) const;
    std::vector<Account> listAccounts() const;
    std::size_t countAccounts() const;

private:
    SQLiteDatabase& database_;
    CredentialStore& credentials_;

    std::string nextAccountId(const std::string& provider) const;
};

}  // namespace routerai
