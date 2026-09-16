#include "core/AccountManager.hpp"
#include "core/Quota.hpp"
#include "providers/codex/CodexProvider.hpp"
#include "storage/SQLiteDatabase.hpp"

#include <CLI/CLI.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

namespace {

void printAccounts(const std::vector<routerai::Account>& accounts) {
    if (accounts.empty()) {
        std::cout << "No accounts configured.\n";
        return;
    }

    std::cout << std::left
              << std::setw(14) << "ID"
              << std::setw(12) << "PROVIDER"
              << std::setw(18) << "STATUS"
              << std::setw(10) << "PRIORITY"
              << "NAME\n";

    std::cout << std::string(72, '-') << '\n';

    for (const auto& account : accounts) {
        std::cout << std::left
                  << std::setw(14) << account.id
                  << std::setw(12) << account.provider
                  << std::setw(18) << routerai::toString(account.status)
                  << std::setw(10) << account.priority
                  << account.displayName << '\n';
    }
}

void printAccount(const routerai::Account& account) {
    std::cout << "ID           : " << account.id << '\n';
    std::cout << "Provider     : " << account.provider << '\n';
    std::cout << "Status       : " << routerai::toString(account.status) << '\n';
    std::cout << "Priority     : " << account.priority << '\n';
    std::cout << "Runtime home : " << account.runtimeHome << '\n';
}

std::string formatDuration(const std::optional<std::int64_t>& minutes) {
    if (!minutes) {
        return "unknown";
    }
    if (*minutes % (24 * 60) == 0) {
        return std::to_string(*minutes / (24 * 60)) + "d";
    }
    if (*minutes % 60 == 0) {
        return std::to_string(*minutes / 60) + "h";
    }
    return std::to_string(*minutes) + "m";
}

std::string formatResetTime(const std::optional<std::int64_t>& unixSeconds) {
    if (!unixSeconds) {
        return "unknown";
    }

    const std::time_t value = static_cast<std::time_t>(*unixSeconds);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &value);
#else
    localtime_r(&value, &local);
#endif

    std::ostringstream output;
    output << std::put_time(&local, "%Y-%m-%d %H:%M:%S");
    return output.str();
}

void printQuota(
    const routerai::Account& account,
    const routerai::QuotaSnapshot& snapshot) {
    std::cout << "Account      : " << account.id << '\n';
    std::cout << "Provider     : " << account.provider << '\n';

    if (!snapshot.accountId.empty()) {
        std::cout << "Provider ID  : " << snapshot.accountId << '\n';
    }

    std::cout << "Usage        : ";
    if (!snapshot.ordinaryUsageAllowed) {
        std::cout << "UNKNOWN\n";
    } else {
        std::cout << (*snapshot.ordinaryUsageAllowed ? "ALLOWED" : "BLOCKED") << '\n';
    }

    if (snapshot.buckets.empty()) {
        std::cout << "Quota        : no rate-limit buckets returned\n";
        return;
    }

    for (const auto& bucket : snapshot.buckets) {
        std::cout << '\n';
        std::cout << "Bucket       : "
                  << (bucket.limitId.empty() ? "default" : bucket.limitId) << '\n';
        if (!bucket.limitName.empty()) {
            std::cout << "Name         : " << bucket.limitName << '\n';
        }
        if (!bucket.model.empty()) {
            std::cout << "Model        : " << bucket.model << '\n';
        }
        if (!bucket.planType.empty()) {
            std::cout << "Plan         : " << bucket.planType << '\n';
        }
        if (!bucket.reachedType.empty()) {
            std::cout << "Reached      : " << bucket.reachedType << '\n';
        }

        for (const auto& window : bucket.windows) {
            const double remaining = std::clamp(100.0 - window.usedPercent, 0.0, 100.0);
            std::cout << "  " << std::left << std::setw(10) << window.name
                      << "used " << std::fixed << std::setprecision(1)
                      << window.usedPercent << "%"
                      << " | remaining " << remaining << "%"
                      << " | window " << formatDuration(window.windowDurationMinutes)
                      << " | reset " << formatResetTime(window.resetsAtUnix)
                      << '\n';
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    try {
        routerai::SQLiteDatabase database("router.db");
        database.initialize();
        routerai::AccountManager accounts(database);

        CLI::App app{"routerAI - console-first AI account router"};
        app.set_version_flag("--version", "routerAI 0.3.0");
        app.require_subcommand(1);

        auto* status = app.add_subcommand("status", "Show local router status");
        status->callback([&]() {
            std::cout << "Router:   OK\n";
            std::cout << "Database: OK (" << database.path() << ")\n";
            std::cout << "Accounts: " << accounts.countAccounts() << '\n';
        });

        auto* doctor = app.add_subcommand("doctor", "Check local runtime dependencies");
        doctor->callback([&]() {
            routerai::CodexProvider codex;
            const bool installed = codex.cliInstalled();

            std::cout << "Database  : OK (" << database.path() << ")\n";
            std::cout << "Codex CLI : " << (installed ? "OK" : "MISSING") << '\n';
            if (installed) {
                std::cout << "Version   : " << codex.cliVersion() << '\n';
            } else {
                std::cout << "Action    : install Codex CLI and ensure `codex` is on PATH\n";
            }
        });

        std::string quotaAccountId;
        auto* quota = app.add_subcommand("quota", "Read provider quota for one or all accounts");
        quota->add_option("account-id", quotaAccountId, "Optional account ID, for example codex-01");
        quota->callback([&]() {
            if (!quotaAccountId.empty()) {
                const auto account = accounts.findAccount(quotaAccountId);
                if (!account) {
                    throw std::runtime_error("Account not found: " + quotaAccountId);
                }
                printQuota(*account, accounts.readQuota(quotaAccountId));
                return;
            }

            bool found = false;
            for (const auto& account : accounts.listAccounts()) {
                if (account.provider != "codex") {
                    continue;
                }
                found = true;
                try {
                    printQuota(account, accounts.readQuota(account.id));
                } catch (const std::exception& exception) {
                    std::cout << "Account      : " << account.id << '\n';
                    std::cout << "Quota        : ERROR\n";
                    std::cout << "Detail       : " << exception.what() << '\n';
                }
                std::cout << std::string(72, '-') << '\n';
            }

            if (!found) {
                std::cout << "No Codex accounts configured.\n";
            }
        });

        auto* account = app.add_subcommand("account", "Manage provider accounts");
        account->require_subcommand(1);

        bool refreshList = false;
        auto* list = account->add_subcommand("list", "List configured accounts");
        list->add_flag("--refresh", refreshList, "Refresh provider authentication status first");
        list->callback([&]() {
            if (refreshList) {
                accounts.refreshAllAccountStatuses();
            }
            printAccounts(accounts.listAccounts());
        });

        auto* add = account->add_subcommand("add", "Add a provider account");
        add->require_subcommand(1);

        bool loginAfterAdd = false;
        bool addWithBrowser = false;
        auto* addCodex = add->add_subcommand("codex", "Add a Codex ChatGPT account");
        addCodex->add_flag("--login", loginAfterAdd, "Start authentication after creating the account");
        addCodex->add_flag("--browser", addWithBrowser, "Use browser callback login instead of device-code login");
        addCodex->callback([&]() {
            const auto created = accounts.addCodexAccount();
            std::cout << "Codex account created.\n\n";
            printAccount(created);

            if (!loginAfterAdd && !addWithBrowser) {
                std::cout << "\nNext: router account login " << created.id << '\n';
                return;
            }

            std::cout << "\nStarting Codex authentication...\n";
            const auto outcome = accounts.loginAccount(created.id, addWithBrowser);
            std::cout << (outcome.result.success ? "Authentication successful.\n" : "Authentication failed.\n");
            if (!outcome.result.detail.empty()) {
                std::cout << outcome.result.detail << '\n';
            }
        });

        std::string loginAccountId;
        bool loginWithBrowser = false;
        auto* login = account->add_subcommand("login", "Authenticate an existing account");
        login->add_option("account-id", loginAccountId, "Account ID, for example codex-01")->required();
        login->add_flag("--browser", loginWithBrowser, "Use browser callback login instead of device-code login");
        login->callback([&]() {
            const auto outcome = accounts.loginAccount(loginAccountId, loginWithBrowser);
            std::cout << (outcome.result.success ? "Authentication successful.\n" : "Authentication failed.\n");
            printAccount(outcome.account);
            if (!outcome.result.detail.empty()) {
                std::cout << "Detail       : " << outcome.result.detail << '\n';
            }
        });

        std::string statusAccountId;
        auto* accountStatus = account->add_subcommand("status", "Refresh and show account authentication status");
        accountStatus->add_option("account-id", statusAccountId, "Account ID, for example codex-01")->required();
        accountStatus->callback([&]() {
            const auto outcome = accounts.refreshAccountStatus(statusAccountId);
            printAccount(outcome.account);
            if (!outcome.auth.detail.empty()) {
                std::cout << "Auth detail  : " << outcome.auth.detail << '\n';
            }
        });

        CLI11_PARSE(app, argc, argv);
        return 0;
    } catch (const std::exception& exception) {
        spdlog::error("{}", exception.what());
        return 1;
    }
}
