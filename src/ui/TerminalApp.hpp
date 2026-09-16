#pragma once

#include "core/Account.hpp"
#include "core/AccountManager.hpp"
#include "core/Quota.hpp"
#include "storage/SQLiteDatabase.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace routerai {

class TerminalApp {
public:
    TerminalApp(SQLiteDatabase& database, AccountManager& accounts);

    int run();

private:
    SQLiteDatabase& database_;
    AccountManager& accounts_;

    void printHeader() const;
    void printMainMenu() const;
    void showDashboard() const;
    void showAccounts() const;
    void addCodexAccount();
    void loginAccount();
    void refreshAccount();
    void showQuota();
    void showQuotaHistory();
    void showSelectedAccount() const;
    void showDoctor() const;

    std::optional<Account> chooseAccount(const std::string& title) const;
    std::optional<int> readChoice(int minimum, int maximum) const;
    std::size_t readHistoryLimit() const;

    static void printAccountTable(const std::vector<Account>& accounts);
    static void printAccountDetails(const Account& account);
    static void printQuota(const Account& account, const QuotaSnapshot& snapshot);
    static void printQuotaHistory(const std::vector<QuotaHistoryEntry>& entries);
    static std::string formatDuration(const std::optional<std::int64_t>& minutes);
    static std::string formatResetTime(const std::optional<std::int64_t>& unixSeconds);
};

}  // namespace routerai
