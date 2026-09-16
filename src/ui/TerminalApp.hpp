#pragma once

#include "core/Account.hpp"
#include "core/AccountManager.hpp"
#include "core/Quota.hpp"
#include "storage/SQLiteDatabase.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace routerai {

class TerminalApp {
public:
    TerminalApp(SQLiteDatabase& database, AccountManager& accounts);

    int run();

private:
    enum class MainAction {
        Dashboard,
        Accounts,
        AddProvider,
        BestAccount,
        Doctor,
        Exit,
    };

    SQLiteDatabase& database_;
    AccountManager& accounts_;

    MainAction chooseMainAction();
    void showDashboard();
    void manageAccounts();
    void addProvider();
    void addCodexAccount();
    void addZaiAccount();
    void showBestAccount();
    void showDoctor();

    void showAccountDetails(const Account& account);
    void loginAccount(const Account& account);
    void refreshAccount(const Account& account);
    void showQuota(const Account& account);
    void showQuotaHistory(const Account& account);

    std::optional<Account> chooseAccount(const std::string& title);
    int chooseOption(
        const std::string& title,
        const std::vector<std::string>& options,
        const std::string& subtitle = {});

    void showMessage(
        const std::string& title,
        const std::vector<std::string>& lines,
        bool isError = false);
    void showScrollableRows(
        const std::string& title,
        const std::vector<std::string>& rows,
        const std::string& subtitle = {});

    static std::vector<std::string> accountDetailLines(const Account& account);
    static std::string accountIdentity(const Account& account);
    static std::string formatDuration(const std::optional<std::int64_t>& minutes);
    static std::string formatResetTime(const std::optional<std::int64_t>& unixSeconds);
};

}  // namespace routerai
