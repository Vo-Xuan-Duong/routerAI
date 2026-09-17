#include "ui/TerminalApp.hpp"

#include "desktop/DesktopProfileManager.hpp"

namespace routerai {

void TerminalApp::showDesktopProfiles() {
    DesktopProfileManager desktop;

    while (true) {
        const auto applications = desktop.detectApplications();
        std::vector<std::string> entries;
        entries.reserve(applications.size() + 1);
        for (const auto& application : applications) {
            entries.push_back(
                application.displayName + " | " +
                (application.installed ? "INSTALLED" : "NOT DETECTED"));
        }
        entries.push_back("Back");

        const int selected = chooseOption(
            "Desktop Applications",
            entries,
            "Detection and launch only. Account switching remains user-controlled unless a stable external profile API is available.");
        if (selected < 0 || static_cast<std::size_t>(selected) >= applications.size()) {
            return;
        }

        const auto& application = applications[static_cast<std::size_t>(selected)];
        std::vector<std::string> details = {
            "Application : " + application.displayName,
            std::string("Installed   : ") + (application.installed ? "yes" : "no"),
            "Executable  : " + (application.executable.empty() ? std::string("-") : application.executable.string()),
            "",
            application.detail,
        };

        if (!application.installed) {
            showMessage(application.displayName, details);
            continue;
        }

        const int action = chooseOption(
            application.displayName,
            {"Launch application", "View details", "Back"},
            application.detail);
        if (action == 0) {
            const bool launched = desktop.launch(application);
            showMessage(
                launched ? "Desktop launched" : "Desktop launch failed",
                {
                    application.displayName,
                    launched
                        ? "The detected desktop application was launched."
                        : "The detected executable could not be launched.",
                    "Account/session selection remains controlled by the official desktop application."
                },
                !launched);
        } else if (action == 1) {
            showMessage(application.displayName, details);
        }
    }
}

}  // namespace routerai
