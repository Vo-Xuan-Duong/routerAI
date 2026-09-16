#include "ui/TerminalApp.hpp"

#include "providers/codex/CodexProvider.hpp"
#include "providers/zai/ZaiProvider.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include <algorithm>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace routerai {

namespace {

using namespace ftxui;

Color statusColor(AccountStatus status) {
    switch (status) {
        case AccountStatus::Ready: return Color::Green;
        case AccountStatus::Warning: return Color::Yellow;
        case AccountStatus::Limited: return Color::Red;
        case AccountStatus::AuthExpired: return Color::Magenta;
        case AccountStatus::Disabled: return Color::GrayDark;
        case AccountStatus::Error: return Color::Red;
    }
    return Color::White;
}

Element statusBadge(AccountStatus status) {
    return text(" " + toString(status) + " ") |
        bold |
        color(statusColor(status));
}

Element keyHint(const std::string& keys, const std::string& action) {
    return hbox({
        text(" " + keys + " ") | bold | color(Color::Cyan),
        text(action) | dim,
    });
}

Element appHeader(const std::string& subtitle = {}) {
    Elements items;
    items.push_back(text(" routerAI ") | bold | color(Color::Cyan));
    items.push_back(text("0.5.0") | dim);
    if (!subtitle.empty()) {
        items.push_back(text("  /  " + subtitle) | dim);
    }
    items.push_back(filler());
    items.push_back(text("Multi-provider Account Router ") | dim);
    return hbox(std::move(items));
}

Element accountCard(const Account& account) {
    const std::string identity = account.email.empty()
        ? account.displayName
        : account.email;

    Elements rows = {
        text(account.id) | bold | color(Color::Cyan),
        separator(),
        hbox({text("Status    : "), statusBadge(account.status)}),
        text("Provider  : " + account.provider),
        text("Plan      : " + (account.planType.empty() ? std::string("-") : account.planType)),
        text("Priority  : " + std::to_string(account.priority)),
        text("Account   : " + (identity.empty() ? std::string("-") : identity)),
        text("Runtime   : " + account.runtimeHome) | dim,
    };

    return vbox(std::move(rows)) | border | flex;
}

std::string percentText(double value) {
    std::ostringstream output;
    output << std::fixed << std::setprecision(1) << value << '%';
    return output.str();
}

Color quotaColor(double usedPercent) {
    if (usedPercent >= 90.0) {
        return Color::Red;
    }
    if (usedPercent >= 70.0) {
        return Color::Yellow;
    }
    return Color::Green;
}

}  // namespace

TerminalApp::TerminalApp(SQLiteDatabase& database, AccountManager& accounts)
    : database_(database), accounts_(accounts) {}

int TerminalApp::run() {
    while (true) {
        const MainAction action = chooseMainAction();
        if (action == MainAction::Exit) {
            return 0;
        }

        try {
            switch (action) {
                case MainAction::Dashboard: showDashboard(); break;
                case MainAction::Accounts: manageAccounts(); break;
                case MainAction::AddProvider: addProvider(); break;
                case MainAction::BestAccount: showBestAccount(); break;
                case MainAction::Doctor: showDoctor(); break;
                case MainAction::Exit: return 0;
            }
        } catch (const std::exception& exception) {
            showMessage("Operation failed", {exception.what()}, true);
        }
    }
}

TerminalApp::MainAction TerminalApp::chooseMainAction() {
    std::vector<std::string> entries = {
        "Dashboard",
        "Accounts",
        "Add Provider",
        "Best account",
        "Doctor",
        "Exit",
    };
    int selected = 0;
    MainAction action = MainAction::Exit;

    const auto accountSnapshot = accounts_.listAccounts();
    std::size_t ready = 0;
    std::size_t warning = 0;
    std::size_t unavailable = 0;
    for (const auto& account : accountSnapshot) {
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

    auto menu = Menu(&entries, &selected);
    auto screen = ScreenInteractive::Fullscreen();

    auto renderer = Renderer(menu, [&] {
        Element preview;
        switch (selected) {
            case 0:
                preview = vbox({
                    text("System overview") | bold,
                    separator(),
                    hbox({text("Accounts     "), text(std::to_string(accountSnapshot.size())) | bold}),
                    hbox({text("Ready        "), text(std::to_string(ready)) | color(Color::Green) | bold}),
                    hbox({text("Warning      "), text(std::to_string(warning)) | color(Color::Yellow) | bold}),
                    hbox({text("Unavailable  "), text(std::to_string(unavailable)) | color(Color::Red) | bold}),
                    text(""),
                    text("Database: " + database_.path()) | dim,
                });
                break;
            case 1:
                preview = vbox({
                    text("Account management") | bold,
                    separator(),
                    text("Manage accounts from all configured providers."),
                    text("Codex accounts expose login, quota and history."),
                    text("Z.ai / Antigravity actions are provider-specific.") | dim,
                });
                break;
            case 2:
                preview = vbox({
                    text("Add Provider") | bold,
                    separator(),
                    text("Codex") | color(Color::Cyan),
                    text("Google Antigravity") | color(Color::Cyan),
                    text("Z.ai") | color(Color::Cyan),
                    text(""),
                    text("Choose the provider first, then configure its account or project.") | dim,
                });
                break;
            case 3:
                preview = vbox({
                    text("Best account") | bold,
                    separator(),
                    text("Shows the current selector result."),
                    text("Provider routing groups will extend this to Codex, Antigravity and Z.ai.") | dim,
                });
                break;
            case 4:
                preview = vbox({
                    text("Doctor") | bold,
                    separator(),
                    text("Checks local database and provider runtimes."),
                });
                break;
            default:
                preview = vbox({
                    text("Exit routerAI") | bold,
                    separator(),
                    text("Close the terminal interface."),
                });
                break;
        }

        return vbox({
            appHeader(),
            separator(),
            hbox({
                vbox({
                    text(" Navigation ") | bold,
                    separator(),
                    menu->Render() | frame | flex,
                }) | border | size(WIDTH, EQUAL, 28),
                preview | border | flex,
            }) | flex,
            separator(),
            hbox({
                keyHint("Up/Down", "navigate"),
                text("   "),
                keyHint("Enter", "select"),
                text("   "),
                keyHint("Esc / q", "exit"),
            }),
        }) | border;
    });

    auto component = CatchEvent(renderer, [&](Event event) {
        if (event == Event::Return) {
            action = static_cast<MainAction>(selected);
            screen.ExitLoopClosure()();
            return true;
        }
        if (event == Event::Escape || event == Event::Character("q")) {
            action = MainAction::Exit;
            screen.ExitLoopClosure()();
            return true;
        }
        return false;
    });

    screen.Loop(component);
    return action;
}

void TerminalApp::showDashboard() {
    auto snapshot = accounts_.listAccounts();
    std::string notice;
    auto screen = ScreenInteractive::Fullscreen();

    auto renderer = Renderer([&] {
        std::size_t ready = 0;
        std::size_t warning = 0;
        std::size_t unavailable = 0;
        for (const auto& account : snapshot) {
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

        Elements accountRows;
        const std::size_t visibleCount = std::min<std::size_t>(snapshot.size(), 12);
        for (std::size_t i = 0; i < visibleCount; ++i) {
            const auto& account = snapshot[i];
            accountRows.push_back(hbox({
                text(account.id) | size(WIDTH, EQUAL, 14),
                text(account.provider) | size(WIDTH, EQUAL, 12),
                text(account.planType.empty() ? "-" : account.planType) | size(WIDTH, EQUAL, 14),
                statusBadge(account.status) | size(WIDTH, EQUAL, 18),
                text(accountIdentity(account)) | flex,
            }));
        }
        if (snapshot.size() > visibleCount) {
            accountRows.push_back(
                text("... " + std::to_string(snapshot.size() - visibleCount) + " more accounts") | dim);
        }
        if (snapshot.empty()) {
            accountRows.push_back(text("No accounts configured yet.") | dim);
        }

        return vbox({
            appHeader("Dashboard"),
            separator(),
            hbox({
                vbox({
                    text(" Accounts ") | bold,
                    text(std::to_string(snapshot.size())) | bold | center,
                }) | border | flex,
                vbox({
                    text(" Ready ") | bold,
                    text(std::to_string(ready)) | bold | color(Color::Green) | center,
                }) | border | flex,
                vbox({
                    text(" Warning ") | bold,
                    text(std::to_string(warning)) | bold | color(Color::Yellow) | center,
                }) | border | flex,
                vbox({
                    text(" Unavailable ") | bold,
                    text(std::to_string(unavailable)) | bold | color(Color::Red) | center,
                }) | border | flex,
            }),
            vbox({
                hbox({
                    text("ID") | bold | size(WIDTH, EQUAL, 14),
                    text("PROVIDER") | bold | size(WIDTH, EQUAL, 12),
                    text("PLAN") | bold | size(WIDTH, EQUAL, 14),
                    text("STATUS") | bold | size(WIDTH, EQUAL, 18),
                    text("ACCOUNT") | bold | flex,
                }),
                separator(),
                vbox(std::move(accountRows)),
            }) | border | flex,
            notice.empty() ? text("") : text(notice) | color(Color::Cyan),
            separator(),
            hbox({
                keyHint("r", "refresh supported accounts"),
                text("   "),
                keyHint("Enter / Esc / q", "back"),
            }),
        }) | border;
    });

    auto component = CatchEvent(renderer, [&](Event event) {
        if (event == Event::Character("r")) {
            try {
                accounts_.refreshAllAccountStatuses();
                snapshot = accounts_.listAccounts();
                notice = "Supported accounts refreshed.";
            } catch (const std::exception& exception) {
                notice = "Refresh error: " + std::string(exception.what());
            }
            return true;
        }
        if (event == Event::Return || event == Event::Escape || event == Event::Character("q")) {
            screen.ExitLoopClosure()();
            return true;
        }
        return false;
    });

    screen.Loop(component);
}

void TerminalApp::manageAccounts() {
    while (true) {
        const auto account = chooseAccount("Accounts");
        if (!account) {
            return;
        }

        if (account->provider != "codex") {
            const int action = chooseOption(
                account->id,
                {"Details", "Provider setup status", "Back"},
                accountIdentity(*account));
            if (action == 0) {
                showAccountDetails(*account);
            } else if (action == 1 && account->provider == "zai") {
                showMessage(
                    "Z.ai provider",
                    {
                        "Authentication mode: API key",
                        "Coding Plan endpoint: " + ZaiProvider::codingBaseUrl(),
                        "General API endpoint: " + ZaiProvider::generalBaseUrl(),
                        "Credential validation and request execution are the next adapter step.",
                        "Public machine-readable Coding Plan quota API is not documented yet."
                    });
            } else if (action == 1) {
                showMessage(
                    "Provider setup",
                    {"This provider adapter is not wired yet."});
            }
            continue;
        }

        const int action = chooseOption(
            account->id,
            {"Details", "Login", "Refresh", "Quota", "Quota history", "Back"},
            accountIdentity(*account));

        switch (action) {
            case 0: showAccountDetails(*account); break;
            case 1: loginAccount(*account); break;
            case 2: refreshAccount(*account); break;
            case 3: showQuota(*account); break;
            case 4: showQuotaHistory(*account); break;
            default: break;
        }
    }
}

void TerminalApp::addProvider() {
    const int provider = chooseOption(
        "Add Provider",
        {"Codex", "Google Antigravity", "Z.ai", "Back"},
        "Choose the account/project provider to configure");

    if (provider == 0) {
        addCodexAccount();
        return;
    }
    if (provider == 1) {
        showMessage(
            "Google Antigravity",
            {
                "Provider slot reserved.",
                "Antigravity account/project and quota adapter will use documented Google interfaces.",
                "Consumer OAuth credentials will not be repurposed as a third-party proxy credential."
            });
        return;
    }
    if (provider == 2) {
        addZaiAccount();
    }
}

void TerminalApp::addCodexAccount() {
    const Account created = accounts_.addCodexAccount();
    const int choice = chooseOption(
        "Codex account created",
        {"Authenticate with device code", "Authenticate in browser", "Do this later"},
        created.id);

    if (choice == 0) {
        loginAccount(created);
        return;
    }
    if (choice == 1) {
        const auto outcome = accounts_.loginAccount(created.id, true);
        auto lines = accountDetailLines(outcome.account);
        if (!outcome.result.detail.empty()) {
            lines.push_back("Auth: " + outcome.result.detail);
        }
        showMessage(
            outcome.result.success ? "Authentication successful" : "Authentication failed",
            lines,
            !outcome.result.success);
        return;
    }

    showAccountDetails(created);
}

void TerminalApp::addZaiAccount() {
    const Account created = accounts_.addZaiAccount();
    auto lines = accountDetailLines(created);
    lines.push_back("Auth mode: Z.ai API key");
    lines.push_back("Coding Plan: " + ZaiProvider::codingBaseUrl());
    lines.push_back("General API: " + ZaiProvider::generalBaseUrl());
    lines.push_back("Next: wire secure API-key entry/validation in the provider adapter.");
    showMessage("Z.ai provider added", lines);
}

void TerminalApp::showBestAccount() {
    const auto selected = accounts_.selectAccount("codex");
    if (!selected) {
        showMessage(
            "Best account",
            {"No eligible Codex account is currently available."},
            true);
        return;
    }

    auto lines = accountDetailLines(selected->account);
    lines.push_back(
        "Latest usage: " +
        (selected->latestUsedPercent
            ? percentText(*selected->latestUsedPercent)
            : std::string("unknown")));
    lines.push_back("Routing groups for Z.ai/Antigravity will extend this selector.");
    showMessage("Selected account", lines);
}

void TerminalApp::showDoctor() {
    CodexProvider codex;
    const bool installed = codex.cliInstalled();

    std::vector<std::string> lines = {
        "Database     : OK (" + database_.path() + ")",
        std::string("Codex runtime: ") + (installed ? "OK" : "NOT INSTALLED YET"),
        "Z.ai adapter : AVAILABLE (API-key wiring pending)",
        "Antigravity  : PLANNED",
    };
    if (installed) {
        lines.push_back("Codex version: " + codex.cliVersion());
    } else {
        lines.push_back("Codex runtime is installed automatically when authentication needs it.");
    }

    showMessage("Doctor", lines, false);
}

void TerminalApp::showAccountDetails(const Account& account) {
    showMessage("Account details", accountDetailLines(account));
}

void TerminalApp::loginAccount(const Account& account) {
    if (account.provider != "codex") {
        showMessage(
            "Provider authentication",
            {"Interactive login is not available for provider: " + account.provider},
            true);
        return;
    }

    const int method = chooseOption(
        "Authenticate " + account.id,
        {"Device-code login", "Browser callback login", "Back"},
        accountIdentity(account));
    if (method < 0 || method == 2) {
        return;
    }

    const auto outcome = accounts_.loginAccount(account.id, method == 1);
    auto lines = accountDetailLines(outcome.account);
    if (!outcome.result.detail.empty()) {
        lines.push_back("Auth: " + outcome.result.detail);
    }
    showMessage(
        outcome.result.success ? "Authentication successful" : "Authentication failed",
        lines,
        !outcome.result.success);
}

void TerminalApp::refreshAccount(const Account& account) {
    const auto outcome = accounts_.refreshAccountStatus(account.id);
    auto lines = accountDetailLines(outcome.account);
    if (!outcome.auth.detail.empty()) {
        lines.push_back("Auth: " + outcome.auth.detail);
    }
    showMessage("Account refreshed", lines, !outcome.auth.authenticated);
}

void TerminalApp::showQuota(const Account& account) {
    const QuotaSnapshot snapshot = accounts_.readQuota(account.id);
    const Account current = accounts_.findAccount(account.id).value_or(account);
    auto screen = ScreenInteractive::Fullscreen();

    auto renderer = Renderer([&] {
        Elements buckets;
        for (const auto& bucket : snapshot.buckets) {
            Elements windows;
            for (const auto& window : bucket.windows) {
                const double used = std::clamp(window.usedPercent, 0.0, 100.0);
                windows.push_back(vbox({
                    hbox({
                        text(window.name.empty() ? "window" : window.name) |
                            size(WIDTH, EQUAL, 14),
                        gauge(static_cast<float>(used / 100.0)) |
                            color(quotaColor(used)) |
                            flex,
                        text(" " + percentText(used)) | size(WIDTH, EQUAL, 9),
                    }),
                    text(
                        "period " + formatDuration(window.windowDurationMinutes) +
                        "   reset " + formatResetTime(window.resetsAtUnix)) | dim,
                }));
            }

            const std::string bucketName = bucket.limitName.empty()
                ? (bucket.limitId.empty() ? std::string("default") : bucket.limitId)
                : bucket.limitName;
            buckets.push_back(vbox({
                hbox({
                    text(bucketName) | bold,
                    filler(),
                    bucket.model.empty() ? text("") : text(bucket.model) | dim,
                }),
                separator(),
                vbox(std::move(windows)),
            }) | border);
        }

        if (buckets.empty()) {
            buckets.push_back(text("No rate-limit buckets returned.") | dim | border);
        }

        Element usage = text(" UNKNOWN ") | bold | color(Color::Yellow);
        if (snapshot.ordinaryUsageAllowed.has_value()) {
            usage = text(*snapshot.ordinaryUsageAllowed ? " ALLOWED " : " BLOCKED ") |
                bold |
                color(*snapshot.ordinaryUsageAllowed ? Color::Green : Color::Red);
        }

        return vbox({
            appHeader("Quota"),
            separator(),
            hbox({
                vbox({
                    text(current.id) | bold | color(Color::Cyan),
                    text(accountIdentity(current)),
                    text("Plan: " + (current.planType.empty() ? std::string("-") : current.planType)),
                }) | border | flex,
                vbox({
                    text("Usage permission") | bold,
                    usage | center,
                }) | border | size(WIDTH, EQUAL, 24),
            }),
            vbox(std::move(buckets)) | frame | flex,
            separator(),
            keyHint("Enter / Esc / q", "back"),
        }) | border;
    });

    auto component = CatchEvent(renderer, [&](Event event) {
        if (event == Event::Return || event == Event::Escape || event == Event::Character("q")) {
            screen.ExitLoopClosure()();
            return true;
        }
        return false;
    });
    screen.Loop(component);
}

void TerminalApp::showQuotaHistory(const Account& account) {
    const int option = chooseOption(
        "Quota history",
        {"Last 20 rows", "Last 50 rows", "Last 100 rows", "Last 200 rows", "Back"},
        account.id);
    if (option < 0 || option == 4) {
        return;
    }

    constexpr std::size_t limits[] = {20, 50, 100, 200};
    const auto history = accounts_.listQuotaHistory(account.id, limits[option]);
    if (history.empty()) {
        showMessage("Quota history", {"No quota history recorded for " + account.id + "."});
        return;
    }

    std::vector<std::string> rows;
    rows.reserve(history.size());
    for (const auto& entry : history) {
        std::ostringstream row;
        row << entry.capturedAt
            << "  |  " << (entry.limitId.empty() ? "default" : entry.limitId)
            << '/' << (entry.windowName.empty() ? "-" : entry.windowName)
            << "  |  " << std::fixed << std::setprecision(1) << entry.usedPercent << "%"
            << "  |  reset " << formatResetTime(entry.resetsAtUnix);
        rows.push_back(row.str());
    }

    showScrollableRows("Quota history - " + account.id, rows, "Up/Down scroll through snapshots");
}

std::optional<Account> TerminalApp::chooseAccount(const std::string& title) {
    const auto accounts = accounts_.listAccounts();
    if (accounts.empty()) {
        showMessage(title, {"No accounts configured."});
        return std::nullopt;
    }

    std::vector<std::string> entries;
    entries.reserve(accounts.size());
    for (const auto& account : accounts) {
        entries.push_back(
            account.id + "  |  " + account.provider + "  |  " +
            (account.planType.empty() ? std::string("-") : account.planType) + "  |  " +
            accountIdentity(account));
    }

    int selected = 0;
    bool accepted = false;
    auto menu = Menu(&entries, &selected);
    auto screen = ScreenInteractive::Fullscreen();

    auto renderer = Renderer(menu, [&] {
        return vbox({
            appHeader(title),
            separator(),
            hbox({
                vbox({
                    text(" Accounts ") | bold,
                    separator(),
                    menu->Render() | frame | flex,
                }) | border | size(WIDTH, EQUAL, 58),
                accountCard(accounts[static_cast<std::size_t>(selected)]),
            }) | flex,
            separator(),
            hbox({
                keyHint("Up/Down", "choose account"),
                text("   "),
                keyHint("Enter", "open"),
                text("   "),
                keyHint("Esc / q", "back"),
            }),
        }) | border;
    });

    auto component = CatchEvent(renderer, [&](Event event) {
        if (event == Event::Return) {
            accepted = true;
            screen.ExitLoopClosure()();
            return true;
        }
        if (event == Event::Escape || event == Event::Character("q")) {
            screen.ExitLoopClosure()();
            return true;
        }
        return false;
    });

    screen.Loop(component);
    if (!accepted) {
        return std::nullopt;
    }
    return accounts[static_cast<std::size_t>(selected)];
}

int TerminalApp::chooseOption(
    const std::string& title,
    const std::vector<std::string>& options,
    const std::string& subtitle) {
    if (options.empty()) {
        return -1;
    }

    int selected = 0;
    int result = -1;
    auto entries = options;
    auto menu = Menu(&entries, &selected);
    auto screen = ScreenInteractive::Fullscreen();

    auto renderer = Renderer(menu, [&] {
        return vbox({
            appHeader(title),
            subtitle.empty() ? text("") : text(subtitle) | dim,
            separator(),
            menu->Render() | frame | border | flex,
            separator(),
            hbox({
                keyHint("Up/Down", "navigate"),
                text("   "),
                keyHint("Enter", "select"),
                text("   "),
                keyHint("Esc / q", "back"),
            }),
        }) | border;
    });

    auto component = CatchEvent(renderer, [&](Event event) {
        if (event == Event::Return) {
            result = selected;
            screen.ExitLoopClosure()();
            return true;
        }
        if (event == Event::Escape || event == Event::Character("q")) {
            result = -1;
            screen.ExitLoopClosure()();
            return true;
        }
        return false;
    });

    screen.Loop(component);
    return result;
}

void TerminalApp::showMessage(
    const std::string& title,
    const std::vector<std::string>& lines,
    bool isError) {
    auto screen = ScreenInteractive::Fullscreen();
    auto renderer = Renderer([&] {
        Elements content;
        for (const auto& line : lines) {
            content.push_back(paragraph(line));
        }
        if (content.empty()) {
            content.push_back(text("-"));
        }

        Element titleElement = text(title) | bold;
        titleElement = titleElement | color(isError ? Color::Red : Color::Cyan);

        return vbox({
            appHeader(),
            separator(),
            vbox({
                titleElement,
                separator(),
                vbox(std::move(content)),
            }) | border | flex,
            separator(),
            keyHint("Enter / Esc / q", "back"),
        }) | border;
    });

    auto component = CatchEvent(renderer, [&](Event event) {
        if (event == Event::Return || event == Event::Escape || event == Event::Character("q")) {
            screen.ExitLoopClosure()();
            return true;
        }
        return false;
    });
    screen.Loop(component);
}

void TerminalApp::showScrollableRows(
    const std::string& title,
    const std::vector<std::string>& rows,
    const std::string& subtitle) {
    if (rows.empty()) {
        showMessage(title, {"No data."});
        return;
    }

    int selected = 0;
    auto entries = rows;
    auto menu = Menu(&entries, &selected);
    auto screen = ScreenInteractive::Fullscreen();

    auto renderer = Renderer(menu, [&] {
        return vbox({
            appHeader(title),
            subtitle.empty() ? text("") : text(subtitle) | dim,
            separator(),
            menu->Render() | frame | border | flex,
            separator(),
            keyHint("Up/Down", "scroll   Enter / Esc / q back"),
        }) | border;
    });

    auto component = CatchEvent(renderer, [&](Event event) {
        if (event == Event::Return || event == Event::Escape || event == Event::Character("q")) {
            screen.ExitLoopClosure()();
            return true;
        }
        return false;
    });
    screen.Loop(component);
}

std::vector<std::string> TerminalApp::accountDetailLines(const Account& account) {
    return {
        "ID       : " + account.id,
        "Provider : " + account.provider,
        "Account  : " + accountIdentity(account),
        "Plan     : " + (account.planType.empty() ? std::string("-") : account.planType),
        "Status   : " + toString(account.status),
        "Priority : " + std::to_string(account.priority),
        "Runtime  : " + account.runtimeHome,
    };
}

std::string TerminalApp::accountIdentity(const Account& account) {
    if (!account.email.empty()) {
        return account.email;
    }
    if (!account.displayName.empty()) {
        return account.displayName;
    }
    return "-";
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
