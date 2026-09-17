#include "ui/ManagementApp.hpp"

#include "Version.hpp"
#include "core/ConfigManager.hpp"
#include "system/ProcessRunner.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include <algorithm>
#include <iomanip>
#include <map>
#include <sstream>

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

std::optional<double> latestUsage(const std::vector<QuotaHistoryEntry>& history) {
    if (history.empty()) return std::nullopt;
    const auto snapshotId = history.front().snapshotId;
    std::optional<double> highest;
    for (const auto& item : history) {
        if (item.snapshotId != snapshotId) break;
        if (!highest || item.usedPercent > *highest) highest = item.usedPercent;
    }
    return highest;
}

std::string identity(const Account& account) {
    if (!account.email.empty()) return account.email;
    if (!account.displayName.empty()) return account.displayName;
    return "-";
}

std::string percent(double value) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(1) << value << '%';
    return out.str();
}

bool openUrl(const std::string& url) {
#ifdef _WIN32
    return ProcessRunner::runInteractive("start \"\" \"" + url + "\"") == 0;
#elif defined(__APPLE__)
    return ProcessRunner::runInteractive("open '" + url + "'") == 0;
#else
    return ProcessRunner::runInteractive("xdg-open '" + url + "' >/dev/null 2>&1 &") == 0;
#endif
}

}  // namespace

ManagementApp::ManagementApp(
    SQLiteDatabase& database,
    AccountManager& accounts,
    RoutingManager& routing,
    LocalApiServer& api)
    : database_(database),
      accounts_(accounts),
      routing_(routing),
      api_(api),
      providerConsole_(database, accounts, routing, api) {}

int ManagementApp::run() {
    while (true) {
        const int action = chooseOption(
            "routerAI " + std::string(kVersion),
            {
                "Usage Dashboard",
                "Account Controls",
                "Provider Console",
                "Request History",
                "Config Export / Import",
                "Web Admin",
                "Exit",
            },
            "Management control plane. Provider Console contains login, quota and routing tools.");

        try {
            if (action == 0) showUsageDashboard();
            else if (action == 1) manageAccountLifecycle();
            else if (action == 2) providerConsole_.run();
            else if (action == 3) showRequestHistory();
            else if (action == 4) showConfigTransfer();
            else if (action == 5) showWebAdmin();
            else return 0;
        } catch (const std::exception& exception) {
            showMessage("Operation failed", {exception.what()}, true);
        }
    }
}

void ManagementApp::showUsageDashboard() {
    const auto accounts = accounts_.listAccounts();
    std::size_t ready = 0, warning = 0, disabled = 0;
    std::map<std::string, std::size_t> providers;
    struct UsageRow { Account account; std::optional<double> usage; };
    std::vector<UsageRow> usageRows;

    for (const auto& account : accounts) {
        ++providers[account.provider];
        if (!account.enabled) ++disabled;
        else if (account.status == AccountStatus::Ready) ++ready;
        else if (account.status == AccountStatus::Warning) ++warning;
        usageRows.push_back({account, latestUsage(accounts_.listQuotaHistory(account.id, 100))});
    }

    const auto logs = database_.listRequestLogs(1000);
    std::size_t success = 0;
    for (const auto& log : logs) if (log.success) ++success;
    const double successRate = logs.empty()
        ? 100.0
        : 100.0 * static_cast<double>(success) / static_cast<double>(logs.size());

    auto screen = ScreenInteractive::Fullscreen();
    auto renderer = Renderer([&] {
        Elements providerItems;
        for (const auto& [provider, count] : providers) {
            providerItems.push_back(text(provider + ": " + std::to_string(count)));
        }
        if (providerItems.empty()) providerItems.push_back(text("No providers configured") | dim);

        Elements rows;
        for (const auto& row : usageRows) {
            const double ratio = row.usage ? std::clamp(*row.usage / 100.0, 0.0, 1.0) : 0.0;
            Element bar = row.usage
                ? hbox({gauge(ratio) | flex, text(" " + percent(*row.usage)) | size(WIDTH, EQUAL, 8)})
                : text("No quota snapshot") | dim;
            rows.push_back(vbox({
                hbox({
                    text(row.account.id) | bold | size(WIDTH, EQUAL, 18),
                    text(row.account.provider) | size(WIDTH, EQUAL, 14),
                    text(identity(row.account)) | flex,
                    text(row.account.enabled ? toString(row.account.status) : "DISABLED") |
                        color(row.account.enabled ? statusColor(row.account.status) : Color::GrayDark),
                }),
                bar,
            }) | border);
        }
        if (rows.empty()) rows.push_back(text("No accounts configured. Open Provider Console -> Add Provider.") | dim);

        std::ostringstream successText;
        successText << std::fixed << std::setprecision(1) << successRate << '%';
        return vbox({
            hbox({text(" routerAI ") | bold | color(Color::Cyan), text(kVersion) | dim, filler(), text("Usage Dashboard ") | bold}),
            separator(),
            hbox({
                vbox({text("Accounts") | dim, text(std::to_string(accounts.size())) | bold | center}) | border | flex,
                vbox({text("Ready") | dim, text(std::to_string(ready)) | bold | color(Color::Green) | center}) | border | flex,
                vbox({text("Warning") | dim, text(std::to_string(warning)) | bold | color(Color::Yellow) | center}) | border | flex,
                vbox({text("Disabled") | dim, text(std::to_string(disabled)) | bold | center}) | border | flex,
                vbox({text("Req success") | dim, text(successText.str()) | bold | center}) | border | flex,
            }),
            hbox({
                vbox({text("Providers") | bold, separator(), vbox(std::move(providerItems))}) | border | size(WIDTH, EQUAL, 25),
                vbox(std::move(rows)) | frame | flex,
            }),
            separator(), text("Enter / Esc / q  back") | dim,
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

void ManagementApp::manageAccountLifecycle() {
    while (true) {
        const auto accounts = accounts_.listAccounts();
        std::vector<std::string> entries;
        for (const auto& account : accounts) {
            entries.push_back(
                account.id + " | " + account.provider + " | " +
                (account.enabled ? toString(account.status) : "DISABLED") + " | " + identity(account));
        }
        entries.push_back("Back");
        const int selected = chooseOption(
            "Account Controls",
            entries,
            "Enable/disable affects routing. Remove deletes local account credential/profile state.");
        if (selected < 0 || static_cast<std::size_t>(selected) >= accounts.size()) return;

        const Account account = accounts[static_cast<std::size_t>(selected)];
        const std::string toggle = account.enabled ? "Disable account" : "Enable account";
        const int action = chooseOption(
            account.id,
            {toggle, "Remove account", "Details", "Back"},
            account.provider + " / " + account.providerMode + " / " + identity(account));
        if (action == 0) {
            accounts_.setAccountEnabled(account.id, !account.enabled);
            routing_.syncDefaultGroups();
            showMessage("Account updated", {account.id + (account.enabled ? " disabled" : " enabled")});
        } else if (action == 1) {
            const int confirm = chooseOption(
                "Remove " + account.id + "?",
                {"Cancel", "Remove permanently"},
                "This removes local metadata, quota history, routing memberships, credential reference and isolated runtime profile. Request audit history is retained.");
            if (confirm == 1) {
                accounts_.removeAccount(account.id);
                routing_.syncDefaultGroups();
                showMessage("Account removed", {account.id});
            }
        } else if (action == 2) {
            showMessage(account.id, {
                "Provider : " + account.provider,
                "Mode     : " + account.providerMode,
                "Identity : " + identity(account),
                "Status   : " + toString(account.status),
                std::string("Enabled  : ") + (account.enabled ? "yes" : "no"),
                "Priority : " + std::to_string(account.priority),
            });
        }
    }
}

void ManagementApp::showRequestHistory() {
    while (true) {
        const auto logs = database_.listRequestLogs(200);
        std::vector<std::string> rows;
        rows.reserve(logs.size() + 2);
        for (const auto& log : logs) {
            rows.push_back(
                log.createdAt + " | " + log.groupId + " | " +
                (log.provider.empty() ? "-" : log.provider) + " | " +
                std::to_string(log.statusCode) + " | " + std::to_string(log.durationMs) + "ms" +
                (log.streaming ? " | stream" : ""));
        }
        rows.push_back("Clear request history");
        rows.push_back("Back");
        const int selected = chooseOption(
            "Request History",
            rows,
            "Only routing metadata/status/error is stored. Prompt and response bodies are not logged.");
        if (selected < 0 || static_cast<std::size_t>(selected) >= logs.size()) {
            if (selected == static_cast<int>(logs.size())) {
                const int confirm = chooseOption("Clear history?", {"Cancel", "Clear"});
                if (confirm == 1) database_.clearRequestLogs();
                continue;
            }
            return;
        }
        const auto& log = logs[static_cast<std::size_t>(selected)];
        showMessage("Request #" + std::to_string(log.id), {
            "Time     : " + log.createdAt,
            "Group    : " + log.groupId,
            "Provider : " + log.provider,
            "Account  : " + log.accountId,
            "Model    : " + log.model,
            "Status   : " + std::to_string(log.statusCode),
            "Duration : " + std::to_string(log.durationMs) + " ms",
            std::string("Streaming: ") + (log.streaming ? "yes" : "no"),
            "Error    : " + (log.error.empty() ? std::string("-") : log.error),
        }, !log.success);
    }
}

void ManagementApp::showConfigTransfer() {
    ConfigManager configs(database_, routing_);
    while (true) {
        const int action = chooseOption(
            "Config Export / Import",
            {"Export metadata JSON", "Import metadata JSON", "Back"},
            "Secrets and credential references are never exported.");
        if (action == 0) {
            const auto path = promptInput("Export path", "routerai-config.json");
            if (!path || path->empty()) continue;
            const auto result = configs.exportTo(*path);
            showMessage("Config exported", {
                *path,
                std::to_string(result.accounts) + " accounts",
                std::to_string(result.routingGroups) + " routing groups",
                result.detail,
            });
        } else if (action == 1) {
            const auto path = promptInput("Import path", "routerai-config.json");
            if (!path || path->empty()) continue;
            const auto result = configs.importFrom(*path);
            showMessage("Config imported", {
                std::to_string(result.accounts) + " accounts",
                std::to_string(result.routingGroups) + " routing groups",
                result.detail,
            });
        } else {
            return;
        }
    }
}

void ManagementApp::showWebAdmin() {
    while (true) {
        const int action = chooseOption(
            "Web Admin",
            {"Open in browser", "Show local API key", "Rotate local API key", "Back"},
            api_.adminUrl());
        if (action == 0) {
            const bool opened = openUrl(api_.adminUrl());
            showMessage(
                opened ? "Browser opened" : "Could not open browser",
                {api_.adminUrl(), "Use the Local API key to connect."},
                !opened);
        } else if (action == 1) {
            showMessage("Local API key", {api_.apiKey(), "This key controls both /v1 and authenticated /admin/api endpoints."});
        } else if (action == 2) {
            const int confirm = chooseOption(
                "Rotate key?",
                {"Cancel", "Rotate"},
                "Existing clients and Web Admin sessions will immediately need the new key.");
            if (confirm == 1) showMessage("New local API key", {api_.rotateApiKey()});
        } else {
            return;
        }
    }
}

int ManagementApp::chooseOption(
    const std::string& title,
    const std::vector<std::string>& options,
    const std::string& subtitle) {
    if (options.empty()) return -1;
    int selected = 0;
    int result = -1;
    auto entries = options;
    auto menu = Menu(&entries, &selected);
    auto screen = ScreenInteractive::Fullscreen();
    auto renderer = Renderer(menu, [&] {
        return vbox({
            hbox({text(" routerAI ") | bold | color(Color::Cyan), text(kVersion) | dim, filler(), text(title + " ") | bold}),
            separator(),
            subtitle.empty() ? text("") : paragraph(subtitle) | dim,
            menu->Render() | frame | flex,
            separator(),
            text("Up/Down navigate   Enter select   Esc/q back") | dim,
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

std::optional<std::string> ManagementApp::promptInput(
    const std::string& title,
    const std::string& placeholder) {
    std::string value;
    bool accepted = false;
    auto screen = ScreenInteractive::Fullscreen();
    InputOption option;
    option.multiline = false;
    option.on_enter = [&] {
        accepted = true;
        screen.ExitLoopClosure()();
    };
    auto input = Input(&value, placeholder, option);
    auto renderer = Renderer(input, [&] {
        return vbox({
            hbox({text(" routerAI ") | bold | color(Color::Cyan), filler(), text(title + " ") | bold}),
            separator(),
            input->Render() | border,
            separator(),
            text("Enter accept   Esc cancel") | dim,
        }) | border;
    });
    auto component = CatchEvent(renderer, [&](Event event) {
        if (event == Event::Escape) {
            accepted = false;
            screen.ExitLoopClosure()();
            return true;
        }
        return false;
    });
    screen.Loop(component);
    if (!accepted) return std::nullopt;
    if (value.empty()) value = placeholder;
    return value;
}

void ManagementApp::showMessage(
    const std::string& title,
    const std::vector<std::string>& lines,
    bool isError) {
    auto screen = ScreenInteractive::Fullscreen();
    auto renderer = Renderer([&] {
        Elements elements;
        for (const auto& line : lines) elements.push_back(paragraph(line));
        return vbox({
            text(title) | bold | color(isError ? Color::Red : Color::Cyan),
            separator(),
            vbox(std::move(elements)) | frame | flex,
            separator(),
            text("Enter / Esc / q  back") | dim,
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

}  // namespace routerai
