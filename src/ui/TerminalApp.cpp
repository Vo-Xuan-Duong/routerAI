#include "ui/TerminalApp.hpp"

#include "api/LocalApiServer.hpp"
#include "Version.hpp"
#include "core/MaintenanceManager.hpp"
#include "providers/antigravity/AntigravityApiClient.hpp"
#include "providers/antigravity/AntigravityProvider.hpp"
#include "providers/codex/CodexProvider.hpp"
#include "providers/zai/ZaiProvider.hpp"
#include "security/CredentialStore.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include <algorithm>
#include <cctype>
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
    return text(" " + toString(status) + " ") | bold | color(statusColor(status));
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
    items.push_back(text(kVersion) | dim);
    if (!subtitle.empty()) items.push_back(text("  /  " + subtitle) | dim);
    items.push_back(filler());
    items.push_back(text("Multi-provider Account Router ") | dim);
    return hbox(std::move(items));
}

std::string routingStrategyLabel(RoutingStrategy strategy) {
    switch (strategy) {
        case RoutingStrategy::HealthFirst: return "Health first";
        case RoutingStrategy::LeastUsed: return "Least used";
        case RoutingStrategy::Priority: return "Priority";
        case RoutingStrategy::RoundRobin: return "Round robin";
        case RoutingStrategy::Manual: return "Manual";
    }
    return "Health first";
}

RoutingStrategy strategyFromIndex(int index) {
    switch (index) {
        case 1: return RoutingStrategy::LeastUsed;
        case 2: return RoutingStrategy::Priority;
        case 3: return RoutingStrategy::RoundRobin;
        case 4: return RoutingStrategy::Manual;
        default: return RoutingStrategy::HealthFirst;
    }
}

bool fixedManualGroup(const RoutingGroup& group) {
    return group.id == "codex-default" || group.id == "antigravity-default";
}

bool defaultManagedGroup(const RoutingGroup& group) {
    return group.id == "codex-default" ||
           group.id == "antigravity-default" ||
           group.id == "antigravity-api-default" ||
           group.id == "zai-default" ||
           group.id == "mixed-default";
}

bool automaticRoutingCapable(const Account& account) {
    return (account.provider == "zai" && account.providerMode == "general-api") ||
           (account.provider == "antigravity" && account.providerMode == "api-project");
}

bool validRoutingGroupId(const std::string& value) {
    if (value.empty()) return false;
    for (const unsigned char ch : value) {
        if (!std::isalnum(ch) && ch != '-' && ch != '_' && ch != '.') return false;
    }
    return true;
}

Element accountCard(const Account& account) {
    const std::string identity = account.email.empty() ? account.displayName : account.email;
    Elements rows = {
        text(account.id) | bold | color(Color::Cyan),
        separator(),
        hbox({text("Status    : "), statusBadge(account.status)}),
        text("Provider  : " + account.provider),
        text("Mode      : " + (account.providerMode.empty() ? std::string("-") : account.providerMode)),
        text("Plan      : " + (account.planType.empty() ? std::string("-") : account.planType)),
        text("Priority  : " + std::to_string(account.priority)),
        text("Account   : " + (identity.empty() ? std::string("-") : identity)),
        text("Runtime   : " + account.runtimeHome) | dim,
    };
    if (account.consecutiveFailures > 0) {
        rows.push_back(text("Failures  : " + std::to_string(account.consecutiveFailures)) | color(Color::Yellow));
    }
    if (!account.lastError.empty()) rows.push_back(paragraph("Last error: " + account.lastError) | color(Color::Red));
    return vbox(std::move(rows)) | border | flex;
}

std::string percentText(double value) {
    std::ostringstream output;
    output << std::fixed << std::setprecision(1) << value << '%';
    return output.str();
}

Color quotaColor(double usedPercent) {
    if (usedPercent >= 90.0) return Color::Red;
    if (usedPercent >= 70.0) return Color::Yellow;
    return Color::Green;
}

}  // namespace

TerminalApp::TerminalApp(
    SQLiteDatabase& database,
    AccountManager& accounts,
    RoutingManager& routing,
    LocalApiServer& api)
    : database_(database), accounts_(accounts), routing_(routing), api_(api) {}

int TerminalApp::run() {
    while (true) {
        const MainAction action = chooseMainAction();
        if (action == MainAction::Exit) return 0;
        try {
            switch (action) {
                case MainAction::Dashboard: showDashboard(); break;
                case MainAction::Accounts: manageAccounts(); break;
                case MainAction::AddProvider: addProvider(); break;
                case MainAction::RoutingGroups: showRoutingGroups(); break;
                case MainAction::LocalApi: showLocalApi(); break;
                case MainAction::BestAccount: showBestAccount(); break;
                case MainAction::Doctor: showDoctor(); break;
                case MainAction::DesktopProfiles: showDesktopProfiles(); break;
                case MainAction::Exit: return 0;
            }
        } catch (const std::exception& exception) {
            showMessage("Operation failed", {exception.what()}, true);
        }
    }
}

TerminalApp::MainAction TerminalApp::chooseMainAction() {
    std::vector<std::string> entries = {
        "Dashboard", "Accounts", "Add Provider", "Routing Groups",
        "Local API", "Best account", "Doctor", "Desktop Applications", "Exit",
    };
    int selected = 0;
    MainAction action = MainAction::Exit;
    const auto accountSnapshot = accounts_.listAccounts();
    auto menu = Menu(&entries, &selected);
    auto screen = ScreenInteractive::Fullscreen();

    auto renderer = Renderer(menu, [&] {
        std::size_t ready = 0, warning = 0, unavailable = 0;
        for (const auto& account : accountSnapshot) {
            if (account.status == AccountStatus::Ready) ++ready;
            else if (account.status == AccountStatus::Warning) ++warning;
            else ++unavailable;
        }

        Element preview;
        switch (selected) {
            case 0:
                preview = vbox({
                    text("System overview") | bold, separator(),
                    text("Accounts: " + std::to_string(accountSnapshot.size())),
                    text("Ready: " + std::to_string(ready)) | color(Color::Green),
                    text("Warning: " + std::to_string(warning)) | color(Color::Yellow),
                    text("Unavailable: " + std::to_string(unavailable)) | color(Color::Red),
                });
                break;
            case 1: preview = vbox({text("Account management") | bold, separator(), text("Login, refresh, quota, credentials and model discovery.")}); break;
            case 2: preview = vbox({text("Add Provider") | bold, separator(), text("Codex / Google Antigravity / Z.ai")}); break;
            case 3: preview = vbox({text("Routing Groups") | bold, separator(), text("Create API pools or manual groups and edit custom memberships."), text("Health-first / least-used / priority / round-robin / manual") | dim}); break;
            case 4: preview = vbox({text("Local API") | bold, separator(), text(api_.baseUrl()), text(api_.running() ? "RUNNING" : "STOPPED") | color(api_.running() ? Color::Green : Color::Red)}); break;
            case 5: preview = vbox({text("Best account") | bold, separator(), text("Preview the selector result for a routing group.")}); break;
            case 6: preview = vbox({text("Doctor") | bold, separator(), text("Check database, runtimes and localhost API.")}); break;
            case 7: preview = vbox({text("Desktop Applications") | bold, separator(), text("Detect and launch supported Codex/Antigravity desktop applications."), text("Account switching remains user-controlled unless a stable external profile API exists.") | dim}); break;
            default: preview = vbox({text("Exit routerAI") | bold}); break;
        }

        return vbox({
            appHeader(), separator(),
            hbox({
                vbox({text(" Navigation ") | bold, separator(), menu->Render() | frame | flex}) | border | size(WIDTH, EQUAL, 30),
                preview | border | flex,
            }) | flex,
            separator(),
            hbox({keyHint("Up/Down", "navigate"), text("   "), keyHint("Enter", "select"), text("   "), keyHint("Esc / q", "exit")}),
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
        std::size_t ready = 0, warning = 0, unavailable = 0;
        for (const auto& account : snapshot) {
            if (account.status == AccountStatus::Ready) ++ready;
            else if (account.status == AccountStatus::Warning) ++warning;
            else ++unavailable;
        }
        Elements rows;
        for (const auto& account : snapshot) {
            rows.push_back(hbox({
                text(account.id) | size(WIDTH, EQUAL, 17),
                text(account.provider) | size(WIDTH, EQUAL, 14),
                text(account.providerMode.empty() ? "-" : account.providerMode) | size(WIDTH, EQUAL, 20),
                statusBadge(account.status) | size(WIDTH, EQUAL, 18),
                text(accountIdentity(account)) | flex,
            }));
        }
        if (rows.empty()) rows.push_back(text("No accounts configured yet.") | dim);
        return vbox({
            appHeader("Dashboard"), separator(),
            hbox({
                vbox({text(" Accounts ") | bold, text(std::to_string(snapshot.size())) | bold | center}) | border | flex,
                vbox({text(" Ready ") | bold, text(std::to_string(ready)) | color(Color::Green) | bold | center}) | border | flex,
                vbox({text(" Warning ") | bold, text(std::to_string(warning)) | color(Color::Yellow) | bold | center}) | border | flex,
                vbox({text(" Unavailable ") | bold, text(std::to_string(unavailable)) | color(Color::Red) | bold | center}) | border | flex,
            }),
            vbox({
                hbox({text("ID") | bold | size(WIDTH, EQUAL, 17), text("PROVIDER") | bold | size(WIDTH, EQUAL, 14), text("MODE") | bold | size(WIDTH, EQUAL, 20), text("STATUS") | bold | size(WIDTH, EQUAL, 18), text("ACCOUNT") | bold | flex}),
                separator(), vbox(std::move(rows)),
            }) | border | frame | flex,
            notice.empty() ? text("") : text(notice) | color(Color::Cyan), separator(),
            hbox({keyHint("r", "refresh accounts"), text("   "), keyHint("Enter / Esc / q", "back")}),
        }) | border;
    });
    auto component = CatchEvent(renderer, [&](Event event) {
        if (event == Event::Character("r")) {
            try {
                accounts_.refreshAllAccountStatuses();
                routing_.syncDefaultGroups();
                snapshot = accounts_.listAccounts();
                notice = "Accounts and routing groups refreshed.";
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
        if (!account) return;

        if (account->provider == "zai") {
            const int action = chooseOption(account->id, {"Details", "Configure API key", "Refresh", "Models", "Back"}, accountIdentity(*account));
            if (action == 0) showAccountDetails(*account);
            if (action == 1) {
                const auto key = promptInput("Z.ai API key", "Paste API key", true);
                if (key && !key->empty()) {
                    const std::string mode = account->providerMode.empty() ? "general-api" : account->providerMode;
                    const auto outcome = accounts_.configureZaiApiKey(account->id, *key, mode);
                    routing_.syncDefaultGroups();
                    auto lines = accountDetailLines(outcome.account);
                    lines.push_back("Credential: " + outcome.auth.detail);
                    showMessage("Z.ai configured", lines, !outcome.auth.authenticated);
                }
            }
            if (action == 2) refreshAccount(*account);
            if (action == 3) showProviderModels(*account);
            continue;
        }

        if (account->provider == "antigravity" && account->providerMode == "api-project") {
            const int action = chooseOption(account->id, {"Details", "Configure Gemini API key", "Refresh", "Models", "Back"}, "Antigravity managed-agent API project");
            if (action == 0) showAccountDetails(*account);
            if (action == 1) {
                const auto key = promptInput("Gemini API key", "Paste API key", true);
                if (key && !key->empty()) {
                    const auto outcome = accounts_.configureAntigravityApiKey(account->id, *key);
                    routing_.syncDefaultGroups();
                    auto lines = accountDetailLines(outcome.account);
                    lines.push_back("Endpoint: " + AntigravityApiClient::endpoint());
                    lines.push_back("Agent: " + AntigravityApiClient::agentName());
                    lines.push_back("Credential: " + outcome.auth.detail);
                    showMessage("Antigravity API configured", lines, !outcome.auth.authenticated);
                }
            }
            if (action == 2) refreshAccount(*account);
            if (action == 3) showProviderModels(*account);
            continue;
        }

        const int action = chooseOption(account->id, {"Details", "Login", "Refresh", "Quota", "Quota history", "Back"}, accountIdentity(*account));
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
    const int provider = chooseOption("Add Provider", {"Codex", "Google Antigravity", "Z.ai", "Back"}, "Choose a provider");
    if (provider == 0) addCodexAccount();
    if (provider == 1) addAntigravityAccount();
    if (provider == 2) addZaiAccount();
}

void TerminalApp::addCodexAccount() {
    const Account created = accounts_.addCodexAccount();
    routing_.syncDefaultGroups();
    const int choice = chooseOption("Codex account created", {"Authenticate with device code", "Authenticate in browser", "Do this later"}, created.id);
    if (choice == 0) {
        loginAccount(created);
        return;
    }
    if (choice == 1) {
        const auto outcome = accounts_.loginAccount(created.id, true);
        auto lines = accountDetailLines(outcome.account);
        if (!outcome.result.detail.empty()) lines.push_back("Auth: " + outcome.result.detail);
        showMessage(outcome.result.success ? "Authentication successful" : "Authentication failed", lines, !outcome.result.success);
        return;
    }
    showAccountDetails(created);
}

void TerminalApp::addAntigravityAccount() {
    const int mode = chooseOption("Google Antigravity", {"Consumer CLI session", "Gemini API project", "Back"}, "Consumer session exposes local Antigravity quota/profile; API project can participate in mixed routing");
    if (mode < 0 || mode == 2) return;

    if (mode == 1) {
        const Account created = accounts_.addAntigravityApiProject();
        const auto key = promptInput("Configure " + created.id, "Paste Gemini API key", true);
        if (!key || key->empty()) {
            routing_.syncDefaultGroups();
            showAccountDetails(created);
            return;
        }
        const auto outcome = accounts_.configureAntigravityApiKey(created.id, *key);
        routing_.syncDefaultGroups();
        auto lines = accountDetailLines(outcome.account);
        lines.push_back("Endpoint: " + AntigravityApiClient::endpoint());
        lines.push_back("Agent: " + AntigravityApiClient::agentName());
        lines.push_back("Credential: " + outcome.auth.detail);
        if (outcome.auth.authenticated) lines.push_back("This API project participates in antigravity-api-default and mixed-default routing.");
        showMessage("Antigravity API project", lines, !outcome.auth.authenticated);
        return;
    }

    AntigravityProvider provider;
    if (!provider.cliInstalled()) {
        const int action = chooseOption("Antigravity runtime", {"Install official Antigravity CLI", "Cancel"}, "Google's official installer will be executed locally");
        if (action != 0) return;
        const int exitCode = provider.installCli();
        if (exitCode != 0 || !provider.cliInstalled()) {
            showMessage("Antigravity installation failed", {"Official installer exit code: " + std::to_string(exitCode), "You can retry from Add Provider."}, true);
            return;
        }
    }

    const Account created = accounts_.addAntigravityAccount();
    routing_.syncDefaultGroups();
    const int action = chooseOption("Antigravity active session", {"Login / verify Google account", "View details", "Back"}, created.id);
    if (action == 0) loginAccount(created);
    if (action == 1) showAccountDetails(created);
}

void TerminalApp::addZaiAccount() {
    const int modeChoice = chooseOption("Z.ai account mode", {"General API", "Coding Plan", "Back"}, "General API participates in unified routing; Coding Plan is kept separate");
    if (modeChoice < 0 || modeChoice == 2) return;
    const std::string mode = modeChoice == 0 ? "general-api" : "coding-plan";
    const Account created = accounts_.addZaiAccount(mode);
    const auto key = promptInput("Configure " + created.id, "Paste Z.ai API key", true);
    if (!key || key->empty()) {
        routing_.syncDefaultGroups();
        showAccountDetails(created);
        return;
    }
    const auto outcome = accounts_.configureZaiApiKey(created.id, *key, mode);
    routing_.syncDefaultGroups();
    auto lines = accountDetailLines(outcome.account);
    lines.push_back(mode == "general-api" ? "Endpoint: " + ZaiProvider::generalBaseUrl() : "Endpoint: " + ZaiProvider::codingBaseUrl());
    lines.push_back("Credential: " + outcome.auth.detail);
    if (mode == "coding-plan") lines.push_back("Coding Plan is not included in the general-purpose mixed API pool.");
    showMessage("Z.ai configured", lines, !outcome.auth.authenticated);
}

void TerminalApp::showRoutingGroups() {
    while (true) {
        routing_.syncDefaultGroups();
        auto groups = routing_.listGroups();
        std::vector<std::string> entries;
        for (const auto& group : groups) {
            entries.push_back(group.id + "  |  " + routingStrategyLabel(group.strategy) + "  |  " + std::to_string(group.accountIds.size()) + " members");
        }
        entries.push_back("Create custom group");
        entries.push_back("Back");
        const int groupIndex = chooseOption("Routing Groups", entries, "Default groups follow provider membership automatically; custom groups are editable");
        if (groupIndex < 0 || static_cast<std::size_t>(groupIndex) == groups.size() + 1) return;
        if (static_cast<std::size_t>(groupIndex) == groups.size()) {
            createCustomRoutingGroup();
            continue;
        }

        RoutingGroup group = groups[static_cast<std::size_t>(groupIndex)];
        const bool fixedManual = fixedManualGroup(group);
        const bool custom = !defaultManagedGroup(group);
        std::vector<std::string> actions = {
            "Preview selection",
            fixedManual ? "Select manual account" : "Change strategy",
            "Show members",
        };
        if (custom) actions.push_back("Edit members");
        actions.push_back("Back");

        const int action = chooseOption(
            group.id,
            actions,
            fixedManual && group.manualAccountId.empty() ? "Manual group: no account selected yet" : routingStrategyLabel(group.strategy));
        if (action < 0 || static_cast<std::size_t>(action) == actions.size() - 1) continue;

        if (action == 0) {
            const auto decision = routing_.select(group.id);
            if (!decision) {
                showMessage("Routing preview", {fixedManual && group.manualAccountId.empty() ? "No manual account is selected for " + group.id : "No eligible account in " + group.id}, true);
            } else {
                auto lines = accountDetailLines(decision->candidate.account);
                lines.push_back("Group: " + group.id);
                lines.push_back("Strategy: " + routingStrategyLabel(group.strategy));
                if (decision->candidate.latestUsedPercent) lines.push_back("Latest usage: " + percentText(*decision->candidate.latestUsedPercent));
                showMessage("Routing preview", lines);
            }
            continue;
        }

        if (action == 1) {
            if (fixedManual) {
                if (group.accountIds.empty()) {
                    showMessage("Manual routing", {"This group has no accounts."}, true);
                    continue;
                }
                std::vector<std::string> members;
                for (const auto& accountId : group.accountIds) {
                    const auto account = accounts_.findAccount(accountId);
                    members.push_back(account ? account->id + " | " + accountIdentity(*account) + " | " + toString(account->status) : accountId);
                }
                members.push_back("Back");
                const int selected = chooseOption("Select manual account", members, group.id);
                if (selected >= 0 && static_cast<std::size_t>(selected) < group.accountIds.size()) {
                    group.strategy = RoutingStrategy::Manual;
                    group.manualAccountId = group.accountIds[static_cast<std::size_t>(selected)];
                    routing_.saveGroup(group);
                    showMessage("Manual account selected", {group.id + " -> " + group.manualAccountId});
                }
                continue;
            }

            const int strategy = chooseOption("Routing strategy", {"Health first", "Least used", "Priority", "Round robin", "Manual", "Back"}, group.id);
            if (strategy >= 0 && strategy < 5) {
                group.strategy = strategyFromIndex(strategy);
                if (custom) {
                    editRoutingGroupMembers(group);
                    continue;
                }
                if (group.strategy == RoutingStrategy::Manual) {
                    if (group.accountIds.empty()) {
                        showMessage("Manual routing", {"This group has no accounts."}, true);
                        continue;
                    }
                    std::vector<std::string> members = group.accountIds;
                    members.push_back("Back");
                    const int selected = chooseOption("Manual account", members, group.id);
                    if (selected < 0 || static_cast<std::size_t>(selected) >= group.accountIds.size()) continue;
                    group.manualAccountId = group.accountIds[static_cast<std::size_t>(selected)];
                } else {
                    group.manualAccountId.clear();
                }
                routing_.saveGroup(group);
                showMessage("Routing updated", {group.id + " -> " + routingStrategyLabel(group.strategy)});
            }
            continue;
        }

        if (action == 2) {
            std::vector<std::string> rows;
            for (const auto& accountId : group.accountIds) {
                const auto account = accounts_.findAccount(accountId);
                rows.push_back(account ? account->id + " | " + account->provider + " | " + account->providerMode + " | " + toString(account->status) : accountId + " | missing");
            }
            showScrollableRows("Members - " + group.id, rows);
            continue;
        }

        if (custom && action == 3) editRoutingGroupMembers(group);
    }
}

void TerminalApp::createCustomRoutingGroup() {
    const auto id = promptInput("New routing group", "group-id");
    if (!id || id->empty()) return;
    if (!validRoutingGroupId(*id)) {
        showMessage("Invalid group id", {"Use letters, numbers, '-', '_' or '.' only."}, true);
        return;
    }
    if (routing_.findGroup(*id)) {
        showMessage("Routing group exists", {*id + " already exists."}, true);
        return;
    }

    const auto displayName = promptInput("Group display name", "Display name");
    if (!displayName || displayName->empty()) return;
    const int strategy = chooseOption("Routing strategy", {"Health first", "Least used", "Priority", "Round robin", "Manual", "Cancel"}, *id);
    if (strategy < 0 || strategy == 5) return;

    RoutingGroup group;
    group.id = *id;
    group.displayName = *displayName;
    group.strategy = strategyFromIndex(strategy);
    group.enabled = true;
    editRoutingGroupMembers(group);
}

void TerminalApp::editRoutingGroupMembers(RoutingGroup& group) {
    const auto allAccounts = accounts_.listAccounts();
    std::vector<Account> candidates;
    for (const auto& account : allAccounts) {
        if (group.strategy == RoutingStrategy::Manual || automaticRoutingCapable(account)) {
            candidates.push_back(account);
        }
    }
    if (candidates.empty()) {
        showMessage("Routing members", {group.strategy == RoutingStrategy::Manual ? "No accounts are configured." : "No API-capable accounts are configured for automatic routing."}, true);
        return;
    }

    std::vector<std::string> selectedIds;
    for (const auto& existing : group.accountIds) {
        const auto it = std::find_if(candidates.begin(), candidates.end(), [&](const Account& account) { return account.id == existing; });
        if (it != candidates.end()) selectedIds.push_back(existing);
    }

    while (true) {
        std::vector<std::string> options;
        for (const auto& account : candidates) {
            const bool selected = std::find(selectedIds.begin(), selectedIds.end(), account.id) != selectedIds.end();
            options.push_back(std::string(selected ? "[x] " : "[ ] ") + account.id + " | " + account.provider + "/" + account.providerMode + " | " + toString(account.status));
        }
        options.push_back("Save members");
        options.push_back("Cancel");

        const int choice = chooseOption("Members - " + group.id, options, group.strategy == RoutingStrategy::Manual ? "Manual groups may contain consumer profiles." : "Automatic groups only allow API-capable accounts.");
        if (choice < 0 || static_cast<std::size_t>(choice) == candidates.size() + 1) return;
        if (static_cast<std::size_t>(choice) < candidates.size()) {
            const std::string& id = candidates[static_cast<std::size_t>(choice)].id;
            const auto found = std::find(selectedIds.begin(), selectedIds.end(), id);
            if (found == selectedIds.end()) selectedIds.push_back(id);
            else selectedIds.erase(found);
            continue;
        }

        group.accountIds = selectedIds;
        group.lastIndex = -1;
        if (group.strategy == RoutingStrategy::Manual) {
            if (group.accountIds.empty()) {
                showMessage("Manual routing", {"Select at least one account for a manual group."}, true);
                continue;
            }
            std::vector<std::string> members = group.accountIds;
            members.push_back("Cancel");
            const int manual = chooseOption("Manual account", members, group.id);
            if (manual < 0 || static_cast<std::size_t>(manual) >= group.accountIds.size()) continue;
            group.manualAccountId = group.accountIds[static_cast<std::size_t>(manual)];
        } else {
            group.manualAccountId.clear();
        }

        try {
            routing_.saveGroup(group);
            showMessage("Routing group saved", {group.id, routingStrategyLabel(group.strategy), std::to_string(group.accountIds.size()) + " members"});
        } catch (const std::exception& exception) {
            showMessage("Routing group rejected", {exception.what()}, true);
        }
        return;
    }
}

void TerminalApp::showLocalApi() {
    while (true) {
        const int action = chooseOption("Local API", {"View configuration", "Rotate local API key", "Back"}, std::string(api_.running() ? "RUNNING  " : "STOPPED  ") + api_.baseUrl());
        if (action < 0 || action == 2) return;
        if (action == 0) {
            std::vector<std::string> lines = {
                std::string("Status   : ") + (api_.running() ? "RUNNING" : "STOPPED"),
                "Base URL : " + api_.baseUrl(),
                "API key  : " + api_.apiKey(),
                "",
                "OpenAI-compatible endpoint: POST /v1/chat/completions",
                "Select a group with X-Router-Group or model: router/<group>.",
                "GET /v1/models lists executable routing groups as router/<group> models.",
                "stream=true is supported as buffered SSE; token-by-token streaming is not implemented yet.",
                "For mixed groups use router.models.zai and router.models.antigravity for provider-specific models.",
                "Custom routing groups are immediately available after they are saved."
            };
            showMessage("Local API configuration", lines, !api_.running());
            continue;
        }
        const int confirm = chooseOption("Rotate local API key", {"Rotate key now", "Cancel"}, "The current local API key will stop working immediately.");
        if (confirm == 0) {
            const std::string replacement = api_.rotateApiKey();
            showMessage("Local API key rotated", {"New key: " + replacement, "Update clients that connect to " + api_.baseUrl() + "."});
        }
    }
}

void TerminalApp::showBestAccount() {
    routing_.syncDefaultGroups();
    const auto groups = routing_.listGroups();
    if (groups.empty()) {
        showMessage("Best account", {"No routing groups available."}, true);
        return;
    }
    std::vector<std::string> names;
    for (const auto& group : groups) names.push_back(group.id);
    names.push_back("Back");
    const int selectedGroup = chooseOption("Best account", names, "Choose a routing group");
    if (selectedGroup < 0 || static_cast<std::size_t>(selectedGroup) >= groups.size()) return;
    const auto decision = routing_.select(groups[static_cast<std::size_t>(selectedGroup)].id);
    if (!decision) {
        showMessage("Best account", {"No eligible account is currently available."}, true);
        return;
    }
    auto lines = accountDetailLines(decision->candidate.account);
    lines.push_back("Group: " + decision->group.id);
    lines.push_back("Strategy: " + routingStrategyLabel(decision->group.strategy));
    showMessage("Selected account", lines);
}

void TerminalApp::showDoctor() {
    CodexProvider codex;
    AntigravityProvider antigravity;
    CredentialStore credentials;
    MaintenanceManager maintenance(database_, routing_, credentials);
    const auto report = maintenance.inspect();

    const bool codexInstalled = codex.cliInstalled();
    const bool antigravityInstalled = antigravity.cliInstalled();
    std::vector<std::string> lines = {
        "Database               : OK (" + database_.path() + ", " +
            std::to_string(report.databaseBytes) + " bytes)",
        "Request history        : " + std::to_string(report.requestLogRows) +
            " rows (auto retention 30 days / 10,000 rows)",
        "Missing credential refs: " + std::to_string(report.missingCredentialRefs),
        "Invalid routing refs   : " + std::to_string(report.invalidRoutingMembers),
        "Orphan runtime folders : " + std::to_string(report.orphanRuntimeDirectories),
        "Missing Codex runtimes : " + std::to_string(report.missingRuntimeDirectories),
        std::string("Local API              : ") + (api_.running() ? "OK " : "FAILED ") + api_.baseUrl(),
        std::string("Codex runtime          : ") + (codexInstalled ? "OK" : "NOT BOOTSTRAPPED YET"),
        std::string("Antigravity consumer CLI: ") + (antigravityInstalled ? "OK" : "NOT INSTALLED"),
        "Antigravity Agent API  : adapter + credential validation available",
        "Z.ai General API       : documented endpoint adapter available",
        "Maintenance actions    : available from the main Management / Doctor screen",
    };
    if (codexInstalled) lines.push_back("Codex version: " + codex.cliVersion());
    if (antigravityInstalled) lines.push_back("agy version  : " + antigravity.cliVersion());
    showMessage("Doctor", lines, !api_.running());
}

void TerminalApp::showAccountDetails(const Account& account) {
    showMessage("Account details", accountDetailLines(account));
}

void TerminalApp::showProviderModels(const Account& account) {
    const ProviderModelsOutcome outcome = accounts_.discoverModels(account.id);
    if (!outcome.success) {
        showMessage("Models - " + account.id, {outcome.detail}, true);
        return;
    }
    std::vector<std::string> rows = outcome.models;
    if (rows.empty()) rows.push_back("No compatible models reported.");
    showScrollableRows("Models - " + account.id, rows, outcome.detail);
}

void TerminalApp::loginAccount(const Account& account) {
    if (account.provider == "antigravity") {
        if (account.providerMode == "api-project") {
            showMessage("Antigravity API project", {"This account uses a Gemini API key. Choose Configure Gemini API key from Accounts instead of interactive login."});
            return;
        }
        const auto outcome = accounts_.loginAccount(account.id, false);
        auto lines = accountDetailLines(outcome.account);
        if (!outcome.result.detail.empty()) lines.push_back("Auth: " + outcome.result.detail);
        showMessage(outcome.result.success ? "Antigravity session ready" : "Antigravity login failed", lines, !outcome.result.success);
        return;
    }
    if (account.provider != "codex") {
        showMessage("Login", {"This provider does not use interactive login."}, true);
        return;
    }
    const int method = chooseOption("Authenticate " + account.id, {"Device-code login", "Browser callback login", "Back"}, accountIdentity(account));
    if (method < 0 || method == 2) return;
    const auto outcome = accounts_.loginAccount(account.id, method == 1);
    auto lines = accountDetailLines(outcome.account);
    if (!outcome.result.detail.empty()) lines.push_back("Auth: " + outcome.result.detail);
    showMessage(outcome.result.success ? "Authentication successful" : "Authentication failed", lines, !outcome.result.success);
}

void TerminalApp::refreshAccount(const Account& account) {
    const auto outcome = accounts_.refreshAccountStatus(account.id);
    auto lines = accountDetailLines(outcome.account);
    if (!outcome.auth.detail.empty()) lines.push_back("Auth: " + outcome.auth.detail);
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
                    hbox({text(window.name.empty() ? "window" : window.name) | size(WIDTH, EQUAL, 14), gauge(static_cast<float>(used / 100.0)) | color(quotaColor(used)) | flex, text(" " + percentText(used)) | size(WIDTH, EQUAL, 9)}),
                    text("period " + formatDuration(window.windowDurationMinutes) + "   reset " + formatResetTime(window.resetsAtUnix)) | dim,
                }));
            }
            const std::string bucketName = bucket.limitName.empty() ? (bucket.limitId.empty() ? std::string("default") : bucket.limitId) : bucket.limitName;
            buckets.push_back(vbox({hbox({text(bucketName) | bold, filler(), bucket.model.empty() ? text("") : text(bucket.model) | dim}), separator(), vbox(std::move(windows))}) | border);
        }
        if (buckets.empty()) buckets.push_back(text("No quota buckets returned.") | dim | border);
        Element usage = text(" UNKNOWN ") | color(Color::Yellow) | bold;
        if (snapshot.ordinaryUsageAllowed) {
            usage = text(*snapshot.ordinaryUsageAllowed ? " ALLOWED " : " BLOCKED ") | color(*snapshot.ordinaryUsageAllowed ? Color::Green : Color::Red) | bold;
        }
        return vbox({
            appHeader("Quota"), separator(),
            hbox({vbox({text(current.id) | bold | color(Color::Cyan), text(accountIdentity(current)), text("Provider: " + current.provider)}) | border | flex, vbox({text("Usage permission") | bold, usage | center}) | border | size(WIDTH, EQUAL, 24)}),
            vbox(std::move(buckets)) | frame | flex, separator(), keyHint("Enter / Esc / q", "back"),
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
    const int option = chooseOption("Quota history", {"Last 20 rows", "Last 50 rows", "Last 100 rows", "Last 200 rows", "Back"}, account.id);
    if (option < 0 || option == 4) return;
    constexpr std::size_t limits[] = {20, 50, 100, 200};
    const auto history = accounts_.listQuotaHistory(account.id, limits[option]);
    if (history.empty()) {
        showMessage("Quota history", {"No quota history recorded for " + account.id + "."});
        return;
    }
    std::vector<std::string> rows;
    for (const auto& entry : history) {
        std::ostringstream row;
        row << entry.capturedAt << " | " << (entry.limitId.empty() ? "default" : entry.limitId) << '/' << (entry.windowName.empty() ? "-" : entry.windowName) << " | " << std::fixed << std::setprecision(1) << entry.usedPercent << "%" << " | reset " << formatResetTime(entry.resetsAtUnix);
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
    for (const auto& account : accounts) entries.push_back(account.id + " | " + account.provider + " | " + toString(account.status) + " | " + accountIdentity(account));
    int selected = 0;
    bool accepted = false;
    auto menu = Menu(&entries, &selected);
    auto screen = ScreenInteractive::Fullscreen();
    auto renderer = Renderer(menu, [&] {
        return vbox({
            appHeader(title), separator(),
            hbox({vbox({text(" Accounts ") | bold, separator(), menu->Render() | frame | flex}) | border | size(WIDTH, EQUAL, 58), accountCard(accounts[static_cast<std::size_t>(selected)])}) | flex,
            separator(), hbox({keyHint("Up/Down", "choose"), text("   "), keyHint("Enter", "open"), text("   "), keyHint("Esc / q", "back")}),
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
    if (!accepted) return std::nullopt;
    return accounts[static_cast<std::size_t>(selected)];
}

int TerminalApp::chooseOption(const std::string& title, const std::vector<std::string>& options, const std::string& subtitle) {
    if (options.empty()) return -1;
    int selected = 0, result = -1;
    auto entries = options;
    auto menu = Menu(&entries, &selected);
    auto screen = ScreenInteractive::Fullscreen();
    auto renderer = Renderer(menu, [&] {
        return vbox({appHeader(title), subtitle.empty() ? text("") : paragraph(subtitle) | dim, separator(), menu->Render() | frame | border | flex, separator(), hbox({keyHint("Up/Down", "navigate"), text("   "), keyHint("Enter", "select"), text("   "), keyHint("Esc / q", "back")})}) | border;
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

std::optional<std::string> TerminalApp::promptInput(const std::string& title, const std::string& placeholder, bool password) {
    std::string value;
    bool accepted = false;
    auto screen = ScreenInteractive::Fullscreen();
    InputOption option;
    option.password = password;
    option.multiline = false;
    option.on_enter = [&] { accepted = true; screen.ExitLoopClosure()(); };
    auto input = Input(&value, placeholder, option);
    auto renderer = Renderer(input, [&] {
        return vbox({appHeader(title), separator(), vbox({text(password ? "Secret input" : "Input") | bold, separator(), input->Render()}) | border | flex, separator(), hbox({keyHint("Enter", "accept"), text("   "), keyHint("Esc", "cancel")})}) | border;
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
    return value;
}

void TerminalApp::showMessage(const std::string& title, const std::vector<std::string>& lines, bool isError) {
    auto screen = ScreenInteractive::Fullscreen();
    auto renderer = Renderer([&] {
        Elements content;
        for (const auto& line : lines) content.push_back(paragraph(line));
        if (content.empty()) content.push_back(text("-"));
        Element titleElement = text(title) | bold | color(isError ? Color::Red : Color::Cyan);
        return vbox({appHeader(), separator(), vbox({titleElement, separator(), vbox(std::move(content))}) | border | flex, separator(), keyHint("Enter / Esc / q", "back")}) | border;
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

void TerminalApp::showScrollableRows(const std::string& title, const std::vector<std::string>& rows, const std::string& subtitle) {
    if (rows.empty()) {
        showMessage(title, {"No data."});
        return;
    }
    int selected = 0;
    auto entries = rows;
    auto menu = Menu(&entries, &selected);
    auto screen = ScreenInteractive::Fullscreen();
    auto renderer = Renderer(menu, [&] {
        return vbox({appHeader(title), subtitle.empty() ? text("") : paragraph(subtitle) | dim, separator(), menu->Render() | frame | border | flex, separator(), keyHint("Up/Down", "scroll   Enter / Esc / q back")}) | border;
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
    std::vector<std::string> lines = {
        "ID         : " + account.id,
        "Provider   : " + account.provider,
        "Mode       : " + (account.providerMode.empty() ? std::string("-") : account.providerMode),
        "Account    : " + accountIdentity(account),
        "Plan       : " + (account.planType.empty() ? std::string("-") : account.planType),
        "Status     : " + toString(account.status),
        "Priority   : " + std::to_string(account.priority),
        "Runtime    : " + account.runtimeHome,
        "Credential : " + (account.credentialRef.empty() ? std::string("-") : std::string("configured")),
        "Failures   : " + std::to_string(account.consecutiveFailures),
        "Cooldown   : " + formatResetTime(account.cooldownUntilUnix),
    };
    if (!account.lastError.empty()) lines.push_back("Last error : " + account.lastError);
    return lines;
}

std::string TerminalApp::accountIdentity(const Account& account) {
    if (!account.email.empty()) return account.email;
    if (!account.displayName.empty()) return account.displayName;
    return "-";
}

std::string TerminalApp::formatDuration(const std::optional<std::int64_t>& minutes) {
    if (!minutes) return "unknown";
    if (*minutes % (24 * 60) == 0) return std::to_string(*minutes / (24 * 60)) + "d";
    if (*minutes % 60 == 0) return std::to_string(*minutes / 60) + "h";
    return std::to_string(*minutes) + "m";
}

std::string TerminalApp::formatResetTime(const std::optional<std::int64_t>& unixSeconds) {
    if (!unixSeconds) return "-";
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
