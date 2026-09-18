#pragma once

#include "api/LocalApiServer.hpp"
#include "core/AccountManager.hpp"
#include "core/RoutingManager.hpp"
#include "storage/SQLiteDatabase.hpp"
#include "ui/TerminalApp.hpp"

#include <optional>
#include <string>
#include <vector>

namespace routerai {

class ManagementApp {
public:
    ManagementApp(
        SQLiteDatabase& database,
        AccountManager& accounts,
        RoutingManager& routing,
        LocalApiServer& api);

    int run();

private:
    SQLiteDatabase& database_;
    AccountManager& accounts_;
    RoutingManager& routing_;
    LocalApiServer& api_;
    TerminalApp providerConsole_;

    void showUsageDashboard();
    void showQuotaAdvisor();
    void manageAccountLifecycle();
    void showRequestHistory();
    void showConfigTransfer();
    void showMaintenance();
    void showWebAdmin();

    int chooseOption(
        const std::string& title,
        const std::vector<std::string>& options,
        const std::string& subtitle = {});
    std::optional<std::string> promptInput(
        const std::string& title,
        const std::string& placeholder);
    void showMessage(
        const std::string& title,
        const std::vector<std::string>& lines,
        bool isError = false);
};

}  // namespace routerai
