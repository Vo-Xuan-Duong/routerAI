#include "ui/TerminalApp.hpp"

#include "providers/codex/CodexProvider.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace routerai {

TerminalApp::TerminalApp(SQLiteDatabase& database, AccountManager& accounts)
    : database_(database), accounts_(accounts) {}

int TerminalApp::run() {
    while (true) {
        printHeader();
        printMainMenu();

        const auto choice = readChoice(0, 9);
        if (!choice || *choice == 0) {
            std::cout << "Goodbye.\n";
            return 0;
        }

        std::cout << '\n';
        try {
            switch (*choice) {
                case 1: showDashboard(); break;
                case 2: showAccounts(); break;
                case 3: addCodexAccount(); break;
                case 4: loginAccount(); break;
                case 5: refreshAccount(); break;
                case 6: showQuota(); break;
                case 7: showQuotaHistory(); break;
                case 8: showSelectedAccount(); break;
                case 9: showDoctor(); break;
                default: break;
            }
        } catch (const std::exception& exception) {
            std::cout << "Error: " << exception.what() << '\n';
        }

        std::cout << '\n';
    }
}

void TerminalApp::printHeader() const {
    std::cout << "\n============================================================\n";
    std::cout << " routerAI 0.3.0 - Terminal Account Router\n";
    std::cout << "============================================================\n";
}

void TerminalApp::printMainMenu() const {
    std::cout << "  1. Dashboard\n";
    std::cout << "  2. Accounts\n";
    std::cout << "  3. Add Codex account\n";
    std::cout << "  4. Login account\n";
    std::cout << "  5. Refresh account\n";
    std::cout << "  6. View quota\n";
    std::cout << "  7. Quota history\n";
    std::cout << "  8. Select best account\n";
    std::cout << "  9. Doctor / dependencies\n";
    std::cout << "  0. Exit\n\n";
}

void TerminalApp::showDashboard() const {
    const auto accounts = accounts_.listAccounts();

    std::size_t ready = 0;
    std::size_t warning = 0;
    std::size_t unavailable = 0;
    for (const auto& account : accounts) {
        if (!account.enabled || account.status == AccountStatus::Disabled) {
            ++unavailable;
        } else if (account.status == AccountStatus::Ready) {
            ++ready;
        } else if (account.status == AccountStatus::Warning) {
            ++warning;
        } else {
            ++unavailable;
        }
    }

    std::cout << "Dashboard\n";
    std::cout << "--------\n";
    std::cout << "Database    : " << database_.path() << '\n';
    std::cout << "Accounts    : " << accounts.size() << '\n';
    std::cout << "Ready       : " << ready << '\n';
    std::cout << "Warning     : " << warning << '\n';
    std::cout << "Unavailable : " << unavailable << '\n';

    if (!accounts.empty()) {
        std::cout << '\n';
        printAccountTable(accounts);
    }
}

void TerminalApp::showAccounts() const {
    printAccountTable(accounts_.listAccounts());
}

void TerminalApp::addCodexAccount() {
    const Account created = accounts_.addCodexAccount();
    std::cout << "Account created.\n\n";
    printAccountDetails(created);

    std::cout << "\nAuthenticate now?\n";
    std::cout << "  1. Device-code login\n";
    std::cout << "  2. Browser callback login\n";
    std::cout << "  0. Later\n";

    const auto method = readChoice(0, 2);
    if (!method || *method == 0) {
        return;
    }

    std::cout << "\nStarting Codex authentication...\n";
    const auto outcome = accounts_.loginAccount(created.id, *method == 2);
    std::cout << (outcome.result.success
        ? "Authentication successful.\n"
        : "Authentication failed.\n");
    printAccountDetails(outcome.account);
    if (!outcome.result.detail.empty()) {
        std::cout << "Detail       : " << outcome.result.detail << '\n';
    }
}

void TerminalApp::loginAccount() {
    const auto account = chooseAccount("Choose an account to authenticate");
    if (!account) {
        return;
    }

    std::cout << "\nLogin method\n";
    std::cout << "  1. Device-code login\n";
    std::cout << "  2. Browser callback login\n";
    std::cout << "  0. Back\n";

    const auto method = readChoice(0, 2);
    if (!method || *method == 0) {
        return;
    }

    std::cout << "\nStarting Codex authentication...\n";
    const auto outcome = accounts_.loginAccount(account->id, *method == 2);
    std::cout << (outcome.result.success
        ? "Authentication successful.\n"
        : "Authentication failed.\n");
    printAccountDetails(outcome.account);
    if (!outcome.result.detail.empty()) {
        std::cout << "Detail       : " << outcome.result.detail << '\n';
    }
}

void TerminalApp::refreshAccount() {
    const auto account = chooseAccount("Choose an account to refresh");
    if (!account) {
        return;
    }

    const auto outcome = accounts_.refreshAccountStatus(account->id);
    std::cout << "Account refreshed.\n\n";
    printAccountDetails(outcome.account);
    if (!outcome.auth.detail.empty()) {
        std::cout << "Auth detail  : " << outcome.auth.detail << '\n';
    }
}

void TerminalApp::showQuota() {
    const auto account = chooseAccount("Choose an account to read quota");
    if (!account) {
        return;
    }

    const auto snapshot = accounts_.readQuota(account->id);
    printQuota(*account, snapshot);
}

void TerminalApp::showQuotaHistory() {
    const auto account = chooseAccount("Choose an account to view quota history");
    if (!account) {
        return;
    }

    const std::size_t limit = readHistoryLimit();
    printQuotaHistory(accounts_.listQuotaHistory(account->id, limit));
}

void TerminalApp::showSelectedAccount() const {
    const auto selected = accounts_.selectAccount("codex");
    if (!selected) {
        std::cout << "No eligible Codex account is currently available.\n";
        return;
    }

    std::cout << "Best account selected from local health data\n";
    std::cout << "--------------------------------------------\n";
    printAccountDetails(selected->account);
    if (selected->latestUsedPercent) {
        std::cout << "Latest usage : " << std::fixed << std::setprecision(1)
                  << *selected->latestUsedPercent << "%\n";
    } else {
        std::cout << "Latest usage : unknown\n";
    }
}

void TerminalApp::showDoctor() const {
    CodexProvider codex;
    const bool installed = codex.cliInstalled();

    std::cout << "Dependency check\n";
    std::cout << "----------------\n";
    std::cout << "Database  : OK (" << database_.path() << ")\n";
    std::cout << "Codex CLI : " << (installed ? "OK" : "MISSING") << '\n';
    if (installed) {
        std::cout << "Version   : " << codex.cliVersion() << '\n';
    } else {
        std::cout << "Action    : install Codex CLI and ensure `codex` is on PATH\n";
    }
}

std::optional<Account> TerminalApp::chooseAccount(const std::string& title) const {
    const auto accounts = accounts_.listAccounts();
    if (accounts.empty()) {
        std::cout << "No accounts configured.\n";
        return std::nullopt;
    }

    std::cout << title << '\n';
    std::cout << std::string(title.size(), '-') << '\n';
    for (std::size_t index = 0; index < accounts.size(); ++index) {
        const auto& account = accounts[index];
        const std::string identity = account.email.empty()
            ? account.displayName
            : account.email;
        std::cout << "  " << (index + 1) << ". "
                  << account.id << " | "
                  << toString(account.status) << " | "
                  << (account.planType.empty() ? "-" : account.planType) << " | "
                  << identity << '\n';
    }
    std::cout << "  0. Back\n";

    const auto choice = readChoice(0, static_cast<int>(accounts.size()));
    if (!choice || *choice == 0) {
        return std::nullopt;
    }
    return accounts[static_cast<std::size_t>(*choice - 1)];
}

std::optional<int> TerminalApp::readChoice(int minimum, int maximum) const {
    while (true) {
        std::cout << "Select [" << minimum << '-' << maximum << "]: ";
        std::string input;
        if (!std::getline(std::cin, input)) {
            return std::nullopt;
        }

        try {
            std::size_t consumed = 0;
            const int value = std::stoi(input, &consumed);
            if (consumed == input.size() && value >= minimum && value <= maximum) {
                return value;
            }
        } catch (...) {
        }

        std::cout << "Invalid selection. Try again.\n";
    }
}

std::size_t TerminalApp::readHistoryLimit() const {
    while (true) {
        std::cout << "History rows [50]: ";
        std::string input;
        if (!std::getline(std::cin, input) || input.empty()) {
            return 50;
        }

        try {
            std::size_t consumed = 0;
            const unsigned long value = std::stoul(input, &consumed);
            if (consumed == input.size() && value >= 1 && value <= 500) {
                return static_cast<std::size_t>(value);
            }
        } catch (...) {
        }

        std::cout << "Enter a number from 1 to 500, or press Enter for 50.\n";
    }
}

void TerminalApp::printAccountTable(const std::vector<Account>& accounts) {
    if (accounts.empty()) {
        std::cout << "No accounts configured.\n";
        return;
    }

    std::cout << std::left
              << std::setw(14) << "ID"
              << std::setw(12) << "PROVIDER"
              << std::setw(12) << "PLAN"
              << std::setw(18) << "STATUS"
              << std::setw(10) << "PRIORITY"
              << "ACCOUNT\n";
    std::cout << std::string(96, '-') << '\n';

    for (const auto& account : accounts) {
        const std::string identity = account.email.empty()
            ? account.displayName
            : account.email;
        std::cout << std::left
                  << std::setw(14) << account.id
                  << std::setw(12) << account.provider
                  << std::setw(12) << (account.planType.empty() ? "-" : account.planType)
                  << std::setw(18) << toString(account.status)
                  << std::setw(10) << account.priority
                  << identity << '\n';
    }
}

void TerminalApp::printAccountDetails(const Account& account) {
    std::cout << "ID           : " << account.id << '\n';
    std::cout << "Provider     : " << account.provider << '\n';
    if (!account.email.empty()) {
        std::cout << "Email        : " << account.email << '\n';
    }
    if (!account.planType.empty()) {
        std::cout << "Plan         : " << account.planType << '\n';
    }
    std::cout << "Status       : " << toString(account.status) << '\n';
    std::cout << "Priority     : " << account.priority << '\n';
    std::cout << "Runtime home : " << account.runtimeHome << '\n';
}

void TerminalApp::printQuota(const Account& account, const QuotaSnapshot& snapshot) {
    std::cout << "Account      : " << account.id << '\n';
    std::cout << "Provider     : " << account.provider << '\n';
    if (!account.email.empty()) {
        std::cout << "Email        : " << account.email << '\n';
    }
    if (!account.planType.empty()) {
        std::cout << "Plan         : " << account.planType << '\n';
    }
    if (!snapshot.accountId.empty()) {
        std::cout << "Provider ID  : " << snapshot.accountId << '\n';
    }

    std::cout << "Usage        : ";
    if (!snapshot.ordinaryUsageAllowed.has_value()) {
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

void TerminalApp::printQuotaHistory(const std::vector<QuotaHistoryEntry>& entries) {
    if (entries.empty()) {
        std::cout << "No quota history recorded.\n";
        return;
    }

    std::cout << std::left
              << std::setw(21) << "CAPTURED"
              << std::setw(10) << "SNAPSHOT"
              << std::setw(16) << "BUCKET"
              << std::setw(12) << "WINDOW"
              << std::setw(10) << "USED"
              << std::setw(10) << "PERIOD"
              << "RESET\n";
    std::cout << std::string(100, '-') << '\n';

    for (const auto& entry : entries) {
        std::ostringstream used;
        used << std::fixed << std::setprecision(1) << entry.usedPercent << '%';
        std::cout << std::left
                  << std::setw(21) << entry.capturedAt
                  << std::setw(10) << entry.snapshotId
                  << std::setw(16) << (entry.limitId.empty() ? "default" : entry.limitId)
                  << std::setw(12) << (entry.windowName.empty() ? "-" : entry.windowName)
                  << std::setw(10) << used.str()
                  << std::setw(10) << formatDuration(entry.windowDurationMinutes)
                  << formatResetTime(entry.resetsAtUnix)
                  << '\n';
    }
}

std::string TerminalApp::formatDuration(const std::optional<std::int64_t>& minutes) {
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

std::string TerminalApp::formatResetTime(const std::optional<std::int64_t>& unixSeconds) {
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

}  // namespace routerai
