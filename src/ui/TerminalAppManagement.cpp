#include "ui/ManagementApp.hpp"

#include "Version.hpp"
#include "core/ConfigManager.hpp"
#include "core/MaintenanceManager.hpp"
#include "security/CredentialStore.hpp"
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

std::string formatBytes(std::uintmax_t bytes) {
    constexpr double kKiB = 1024.0;
    constexpr double kMiB = kKiB * 1024.0;
    constexpr double kGiB = kMiB * 1024.0;
    std::ostringstream out;
    if (bytes < 1024) {
        out << bytes << " B";
    } else if (static_cast<double>(bytes) < kMiB) {
        out << std::fixed << std::setprecision(1) << static_cast<double>(bytes) / kKiB << " KiB";
    } else if (static_cast<double>(bytes) < kGiB) {
        out << std::fixed << std::setprecision(1) << static_cast<double>(bytes) / kMiB << " MiB";
    } else {
        out << std::fixed << std::setprecision(2) << static_cast<double>(bytes) / kGiB << " GiB";
    }
    return out.str();
}

bool supportsQuotaRefresh(const Account& account) {
    return account.provider == "codex" ||
        (account.provider == "antigravity" && account.providerMode == "consumer-cli");
}

std::string latestUsageText(AccountManager& accounts, const Account& account) {
    const auto usage = latestUsage(accounts.listQuotaHistory(account.id, 100));
    return usage ? percent(*usage) : "-";
}

std::vector<std::string> quotaSnapshotLines(const QuotaSnapshot& snapshot) {
    std::vector<std::string> lines;
    lines.push_back("Account  : " + snapshot.accountId);
    if (snapshot.ordinaryUsageAllowed.has_value()) {
        lines.push_back(
            std::string("Allowed  : ") + (*snapshot.ordinaryUsageAllowed ? "yes" : "no"));
    }

    for (const auto& bucket : snapshot.buckets) {
        const std::string bucketName = !bucket.limitName.empty()
            ? bucket.limitName
            : (!bucket.limitId.empty() ? bucket.limitId : "quota");
        if (bucket.windows.empty()) {
            lines.push_back(bucketName + " : no usage window returned");
            continue;
        }
        for (const auto& window : bucket.windows) {
            std::string line = bucketName;
            if (!window.name.empty()) line += " / " + window.name;
            line += " : " + percent(window.usedPercent);
            if (window.windowDurationMinutes) {
                line += " over " + std::to_string(*window.windowDurationMinutes) + " min";
            }
            lines.push_back(std::move(line));
        }
    }

    if (snapshot.buckets.empty()) {
        lines.push_back("Provider returned no machine-readable quota buckets.");
    }
    return lines;
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
                "Account Overview",
                "Provider Console",
                "Request History",
                "Config Export / Import",
                "Maintenance / Doctor",
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
            else if (action == 5) showMaintenance();
            else if (action == 6) showWebAdmin();
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
        entries.reserve(accounts.size() + 3);

        for (const auto& account : accounts) {
            entries.push_back(
                account.id + " | " + account.provider + "/" + account.providerMode + " | " +
                (account.enabled ? toString(account.status) : "DISABLED") +
                " | quota " + latestUsageText(accounts_, account) +
                " | " + identity(account));
        }

        const int refreshStatusesIndex = static_cast<int>(entries.size());
        entries.push_back("Refresh all account statuses");
        const int refreshQuotasIndex = static_cast<int>(entries.size());
        entries.push_back("Refresh all supported quota snapshots");
        entries.push_back("Back");

        const int selected = chooseOption(
            "Account Overview",
            entries,
            "Select an account to refresh status/quota, enable/disable it, inspect details, or remove it.");

        if (selected == refreshStatusesIndex) {
            std::size_t refreshed = 0;
            std::size_t attention = 0;
            std::size_t failed = 0;
            std::vector<std::string> lines;

            for (const auto& account : accounts) {
                try {
                    const auto outcome = accounts_.refreshAccountStatus(account.id);
                    ++refreshed;
                    if (!outcome.auth.authenticated) {
                        ++attention;
                        lines.push_back(
                            account.id + ": " +
                            (outcome.auth.detail.empty() ? "authentication needs attention" : outcome.auth.detail));
                    }
                } catch (const std::exception& exception) {
                    ++failed;
                    lines.push_back(account.id + ": " + exception.what());
                }
            }

            routing_.syncDefaultGroups();
            lines.insert(lines.begin(), {
                "Refreshed : " + std::to_string(refreshed),
                "Attention : " + std::to_string(attention),
                "Failed    : " + std::to_string(failed),
            });
            showMessage("Account status refresh", lines, failed > 0);
            continue;
        }

        if (selected == refreshQuotasIndex) {
            std::size_t refreshed = 0;
            std::size_t skipped = 0;
            std::size_t failed = 0;
            std::vector<std::string> lines;

            for (const auto& account : accounts) {
                if (!supportsQuotaRefresh(account)) {
                    ++skipped;
                    continue;
                }
                try {
                    accounts_.readQuota(account.id);
                    ++refreshed;
                } catch (const std::exception& exception) {
                    ++failed;
                    lines.push_back(account.id + ": " + exception.what());
                }
            }

            routing_.syncDefaultGroups();
            lines.insert(lines.begin(), {
                "Refreshed : " + std::to_string(refreshed),
                "Skipped   : " + std::to_string(skipped) + " (provider exposes no supported quota reader)",
                "Failed    : " + std::to_string(failed),
            });
            showMessage("Quota refresh", lines, failed > 0);
            continue;
        }

        if (selected < 0 || static_cast<std::size_t>(selected) >= accounts.size()) return;

        const Account account = accounts[static_cast<std::size_t>(selected)];
        const std::string toggle = account.enabled ? "Disable account" : "Enable account";
        const int action = chooseOption(
            account.id,
            {"Refresh status", "Refresh quota", toggle, "Remove account", "Details", "Back"},
            account.provider + " / " + account.providerMode + " / " + identity(account));

        if (action == 0) {
            const auto outcome = accounts_.refreshAccountStatus(account.id);
            routing_.syncDefaultGroups();
            showMessage(
                "Account refreshed",
                {
                    "Status   : " + toString(outcome.account.status),
                    "Identity : " + identity(outcome.account),
                    "Plan     : " + (outcome.account.planType.empty() ? std::string("-") : outcome.account.planType),
                    "Auth     : " + (outcome.auth.detail.empty() ? std::string("-") : outcome.auth.detail),
                },
                !outcome.auth.authenticated);
        } else if (action == 1) {
            if (!supportsQuotaRefresh(account)) {
                showMessage(
                    "Quota unavailable",
                    {
                        "No supported machine-readable quota reader exists for " +
                            account.provider + "/" + account.providerMode + ".",
                        "The account can still be routed/validated using its supported provider interfaces.",
                    });
                continue;
            }
            const auto snapshot = accounts_.readQuota(account.id);
            routing_.syncDefaultGroups();
            showMessage("Quota snapshot", quotaSnapshotLines(snapshot));
        } else if (action == 2) {
            accounts_.setAccountEnabled(account.id, !account.enabled);
            routing_.syncDefaultGroups();
            showMessage("Account updated", {account.id + (account.enabled ? " disabled" : " enabled")});
        } else if (action == 3) {
            const int confirm = chooseOption(
                "Remove " + account.id + "?",
                {"Cancel", "Remove permanently"},
                "This removes local metadata, quota history, routing memberships, credential reference and isolated runtime profile. Request audit history is retained.");
            if (confirm == 1) {
                accounts_.removeAccount(account.id);
                routing_.syncDefaultGroups();
                showMessage("Account removed", {account.id});
            }
        } else if (action == 4) {
            const auto current = accounts_.findAccount(account.id).value_or(account);
            std::vector<std::string> lines = {
                "Provider : " + current.provider,
                "Mode     : " + current.providerMode,
                "Identity : " + identity(current),
                "Status   : " + toString(current.status),
                std::string("Enabled  : ") + (current.enabled ? "yes" : "no"),
                "Priority : " + std::to_string(current.priority),
                "Plan     : " + (current.planType.empty() ? std::string("-") : current.planType),
                "Quota    : " + latestUsageText(accounts_, current),
            };
            if (!current.lastError.empty()) lines.push_back("Last error: " + current.lastError);
            if (current.cooldownUntilUnix) {
                lines.push_back("Cooldown until unix: " + std::to_string(*current.cooldownUntilUnix));
            }
            showMessage(current.id, lines);
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


void ManagementApp::showMaintenance() {
    CredentialStore credentials;
    MaintenanceManager maintenance(database_, routing_, credentials);

    while (true) {
        const auto report = maintenance.inspect();
        const std::string subtitle =
            "DB " + formatBytes(report.databaseBytes) +
            " | request logs " + std::to_string(report.requestLogRows) +
            " | missing secrets " + std::to_string(report.missingCredentialRefs) +
            " | invalid routing " + std::to_string(report.invalidRoutingMembers) +
            " | orphan runtime " + std::to_string(report.orphanRuntimeDirectories);

        const int action = chooseOption(
            "Maintenance / Doctor",
            {
                "Run request-log retention (30 days / 10,000 rows)",
                "Repair invalid routing members (" + std::to_string(report.invalidRoutingMembers) + ")",
                "Remove orphan runtime folders (" + std::to_string(report.orphanRuntimeDirectories) + ")",
                "Diagnostics details",
                "Back",
            },
            subtitle);

        if (action == 0) {
            const auto result = maintenance.pruneRequestLogs();
            showMessage("Request-log retention", {
                "Before  : " + std::to_string(result.beforeRows),
                "Removed : " + std::to_string(result.removedRows),
                "After   : " + std::to_string(result.afterRows),
                "Policy  : newest 10,000 rows, maximum age 30 days",
            });
        } else if (action == 1) {
            if (report.invalidRoutingMembers == 0) {
                showMessage("Routing maintenance", {"No invalid routing members found."});
                continue;
            }
            const auto fixes = maintenance.repairRoutingGroups();
            showMessage("Routing maintenance", {
                "Removed/cleared invalid references: " + std::to_string(fixes),
                "Default routing groups were synchronized after repair.",
            });
        } else if (action == 2) {
            if (report.orphanRuntimeDirectories == 0) {
                showMessage("Runtime maintenance", {"No orphan runtime folders found."});
                continue;
            }
            const int confirm = chooseOption(
                "Remove orphan runtime folders?",
                {"Cancel", "Remove"},
                "Only direct children under .routerai/accounts that no longer match an account ID are removed.");
            if (confirm == 1) {
                const auto removed = maintenance.removeOrphanRuntimeDirectories();
                showMessage("Runtime maintenance", {
                    "Removed orphan runtime folders: " + std::to_string(removed),
                });
            }
        } else if (action == 3) {
            std::vector<std::string> lines = {
                "Database footprint      : " + formatBytes(report.databaseBytes),
                "Request log rows        : " + std::to_string(report.requestLogRows),
                "Missing credential refs : " + std::to_string(report.missingCredentialRefs),
                "Invalid routing refs    : " + std::to_string(report.invalidRoutingMembers),
                "Orphan runtime folders  : " + std::to_string(report.orphanRuntimeDirectories),
                "Missing Codex runtimes  : " + std::to_string(report.missingRuntimeDirectories),
                std::string("Local API               : ") + (api_.running() ? "OK " : "FAILED ") + api_.baseUrl(),
            };

            for (const auto& id : report.missingCredentialAccounts) {
                lines.push_back("Missing credential: " + id);
            }
            for (const auto& entry : report.invalidRoutingEntries) {
                lines.push_back("Invalid routing  : " + entry);
            }
            for (const auto& path : report.orphanRuntimePaths) {
                lines.push_back("Orphan runtime   : " + path.string());
            }
            for (const auto& id : report.missingRuntimeAccounts) {
                lines.push_back("Missing runtime  : " + id);
            }
            showMessage("Doctor details", lines, !api_.running());
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
