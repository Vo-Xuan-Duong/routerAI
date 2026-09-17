#include "desktop/DesktopProfileManager.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

bool hasApplication(
    const std::vector<routerai::DesktopApplication>& applications,
    const std::string& id) {
    for (const auto& application : applications) {
        if (application.id == id) return true;
    }
    return false;
}

}  // namespace

int main() {
    try {
        routerai::DesktopProfileManager desktop;
        const auto applications = desktop.detectApplications();

        require(applications.size() == 2, "desktop manager should expose two supported applications");
        require(hasApplication(applications, "codex-desktop"), "Codex Desktop metadata is missing");
        require(hasApplication(applications, "antigravity-desktop"), "Antigravity Desktop metadata is missing");

        const auto codex = desktop.findApplication("codex-desktop");
        require(codex.has_value(), "findApplication should resolve Codex Desktop");
        require(!codex->displayName.empty(), "Codex Desktop display name must not be empty");
        require(!desktop.findApplication("missing-desktop").has_value(), "unknown desktop id should not resolve");

        routerai::DesktopApplication invalid;
        invalid.id = "invalid";
        invalid.displayName = "Invalid";
        invalid.installed = false;
        require(!desktop.launch(invalid), "non-installed desktop application must not launch");

        invalid.installed = true;
        require(!desktop.launch(invalid), "desktop application without executable must not launch");

        std::cout << "DesktopProfileManagerTests: OK\n";
    } catch (const std::exception& exception) {
        std::cerr << "DesktopProfileManagerTests: FAILED: " << exception.what() << '\n';
        return 1;
    }
    return 0;
}
