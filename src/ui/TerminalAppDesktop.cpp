#include "ui/TerminalApp.hpp"

#include "desktop/DesktopAccountSwitcher.hpp"
#include "desktop/DesktopProfileManager.hpp"

namespace routerai {

void TerminalApp::showDesktopProfiles() {
    DesktopProfileManager desktop;
    DesktopAccountSwitcher switcher;

    while (true) {
        const auto applications = desktop.detectApplications();
        std::vector<std::string> entries;
        entries.reserve(applications.size() + 1);
        for (const auto& application : applications) {
            const auto capability = switcher.capability(application.id);
            entries.push_back(
                application.displayName + " | " +
                (application.installed ? "INSTALLED" : "NOT DETECTED") + " | switch=" +
                (capability.supported ? "SUPPORTED" : "UNAVAILABLE"));
        }
        entries.push_back("Back");

        const int selected = chooseOption(
            "Desktop Applications",
            entries,
            "Detection/launch is available now. Account switching is activated only through a supported provider interface.");
        if (selected < 0 || static_cast<std::size_t>(selected) >= applications.size()) return;

        const auto& application = applications[static_cast<std::size_t>(selected)];
        const auto capability = switcher.capability(application.id);
        std::vector<std::string> details = {
            "Application : " + application.displayName,
            std::string("Installed   : ") + (application.installed ? "yes" : "no"),
            "Executable  : " + (application.executable.empty() ? std::string("-") : application.executable.string()),
            std::string("Switch API  : ") + (capability.supported ? "supported" : "not available"),
            "",
            application.detail,
            capability.detail,
        };

        if (!application.installed) {
            showMessage(application.displayName, details);
            continue;
        }

        const int action = chooseOption(
            application.displayName,
            {"Launch application", "Switch account", "View details", "Back"},
            capability.supported
                ? "A supported desktop account switch adapter is available."
                : "Switch account is present as a future capability but is disabled by the provider adapter in this build.");
        if (action == 0) {
            const bool launched = desktop.launch(application);
            showMessage(
                launched ? "Desktop launched" : "Desktop launch failed",
                {application.displayName, launched ? "The detected desktop application was launched." : "The detected executable could not be launched."},
                !launched);
        } else if (action == 1) {
            if (!capability.supported) {
                showMessage("Desktop switching unavailable", {capability.detail});
                continue;
            }

            const std::string targetProvider = application.id == "codex-desktop" ? "codex" : "antigravity";
            std::vector<Account> providerAccounts;
            std::vector<std::string> accountEntries;
            for (const auto& account : accounts_.listAccounts()) {
                if (account.provider != targetProvider) continue;
                providerAccounts.push_back(account);
                accountEntries.push_back(account.id + " | " + accountIdentity(account));
            }
            accountEntries.push_back("Back");
            const int accountIndex = chooseOption("Switch desktop account", accountEntries, capability.detail);
            if (accountIndex < 0 || static_cast<std::size_t>(accountIndex) >= providerAccounts.size()) continue;

            std::string detail;
            const bool switched = switcher.switchAccount(
                application.id,
                providerAccounts[static_cast<std::size_t>(accountIndex)].id,
                &detail);
            showMessage(switched ? "Desktop account switched" : "Desktop switch failed", {detail}, !switched);
        } else if (action == 2) {
            showMessage(application.displayName, details);
        }
    }
}

}  // namespace routerai
