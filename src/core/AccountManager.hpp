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

struct ProviderModelsOutcome {
    bool success{false};
    std::vector<std::string> models;
    std::string detail;
};

class AccountManager {
public:
    AccountManager(SQLiteDatabase& database, CredentialStore& credentials);

    Account addCodexAccount();
    Account addAntigravityAccount();
    Account addAntigravityApiProject();
    Account addZaiAccount(const std::string& mode = "general-api");

    AccountAuthOutcome configureAntigravityApiKey(
        const std::string& accountId,
        const std::string& apiKey);
    AccountAuthOutcome configureZaiApiKey(
        const std::string& accountId,
        const std::string& apiKey,
        const std::string& mode);

    AccountLoginOutcome loginAccount(const std::string& accountId, bool useBrowser);
    AccountAuthOutcome refreshAccountStatus(const std::string& accountId);
    void refreshAllAccountStatuses();

    Account setAccountEnabled(const std::string& accountId, bool enabled);
    Account setAccountPriority(const std::string& accountId, int priority);
    void removeAccount(const std::string& accountId);

    ProviderModelsOutcome discoverModels(const std::string& accountId) const;

    QuotaSnapshot readQuota(const std::string& accountId);
    std::vector<QuotaHistoryEntry> listQuotaHistory(
        const std::string& accountId,
        std::size_t limit = 50) const;
    std::optional<RoutingCandidate> selectAccount(
        const std::string& provider = "codex") const;
    std::optional<RoutingCandidate> selectAccount(
        const std::string& provider,
        const std::string& providerMode) const;

    std::optional<Account> findAccount(const std::string& accountId) const;
    std::vector<Account> listAccounts() const;
    std::size_t countAccounts() const;

private:
    SQLiteDatabase& database_;
    CredentialStore& credentials_;

    std::string nextAccountId(const std::string& provider) const;
};

}  // namespace routerai
