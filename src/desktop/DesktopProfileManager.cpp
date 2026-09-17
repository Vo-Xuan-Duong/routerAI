#include "desktop/DesktopProfileManager.hpp"

#include "system/ProcessRunner.hpp"

#include <cstdlib>
#include <filesystem>
#include <string>

namespace routerai {

namespace {

std::optional<std::filesystem::path> firstExisting(
    const std::vector<std::filesystem::path>& candidates) {
    for (const auto& candidate : candidates) {
        std::error_code error;
        if (!candidate.empty() && std::filesystem::exists(candidate, error) && !error) {
            return std::filesystem::absolute(candidate, error);
        }
    }
    return std::nullopt;
}

std::string quote(const std::filesystem::path& path) {
#ifdef _WIN32
    std::string escaped;
    for (const char ch : path.string()) {
        if (ch == '"') escaped += "\\\"";
        else escaped += ch;
    }
    return "\"" + escaped + "\"";
#else
    std::string value = path.string();
    std::string quoted = "'";
    for (const char ch : value) {
        if (ch == '\'') quoted += "'\\''";
        else quoted += ch;
    }
    quoted += '\'';
    return quoted;
#endif
}

std::filesystem::path environmentPath(const char* name) {
    if (const char* value = std::getenv(name)) return value;
    return {};
}

}  // namespace

std::vector<DesktopApplication> DesktopProfileManager::detectApplications() const {
    return {detectCodex(), detectAntigravity()};
}

std::optional<DesktopApplication> DesktopProfileManager::findApplication(
    const std::string& id) const {
    for (const auto& application : detectApplications()) {
        if (application.id == id) return application;
    }
    return std::nullopt;
}

bool DesktopProfileManager::launch(const DesktopApplication& application) const {
    if (!application.installed || application.executable.empty()) return false;
#ifdef _WIN32
    const std::string command = "start \"\" " + quote(application.executable);
#elif defined(__APPLE__)
    const std::string command = "open " + quote(application.executable);
#else
    const std::string command = quote(application.executable) + " >/dev/null 2>&1 &";
#endif
    return ProcessRunner::runInteractive(command) == 0;
}

DesktopApplication DesktopProfileManager::detectCodex() {
    DesktopApplication application;
    application.id = "codex-desktop";
    application.displayName = "ChatGPT Desktop / Codex";

#ifdef _WIN32
    const auto local = environmentPath("LOCALAPPDATA");
    std::vector<std::filesystem::path> candidates;
    if (!local.empty()) {
        // Current OpenAI desktop builds expose Codex as a view inside the
        // ChatGPT desktop app. Keep legacy Codex paths as fallbacks for users
        // who have not migrated yet.
        candidates.push_back(local / "Programs" / "ChatGPT" / "ChatGPT.exe");
        candidates.push_back(local / "OpenAI" / "ChatGPT" / "ChatGPT.exe");
        candidates.push_back(local / "Microsoft" / "WindowsApps" / "ChatGPT.exe");
        candidates.push_back(local / "OpenAI" / "Codex" / "Codex.exe");
        candidates.push_back(local / "Programs" / "Codex" / "Codex.exe");
    }
#elif defined(__APPLE__)
    std::vector<std::filesystem::path> candidates = {
        "/Applications/ChatGPT.app",
        "/Applications/Codex.app",
    };
#else
    std::vector<std::filesystem::path> candidates;
#endif

    if (const auto executable = firstExisting(candidates)) {
        application.installed = true;
        application.executable = *executable;
        application.detail =
            "Installed. Codex is available from the ChatGPT desktop application. "
            "Account switching remains controlled by the official desktop app.";
    } else {
        application.detail =
            "ChatGPT/Codex desktop was not detected by known filesystem candidates. "
            "Desktop account switching is not exposed as a stable external profile API.";
    }
    return application;
}

DesktopApplication DesktopProfileManager::detectAntigravity() {
    DesktopApplication application;
    application.id = "antigravity-desktop";
    application.displayName = "Antigravity Desktop";

#ifdef _WIN32
    const auto local = environmentPath("LOCALAPPDATA");
    std::vector<std::filesystem::path> candidates;
    if (!local.empty()) {
        candidates.push_back(local / "Programs" / "antigravity" / "Antigravity.exe");
        candidates.push_back(local / "Programs" / "Antigravity IDE" / "Antigravity IDE.exe");
    }
#elif defined(__APPLE__)
    std::vector<std::filesystem::path> candidates = {
        "/Applications/Antigravity.app",
        "/Applications/Antigravity IDE.app",
    };
#else
    std::vector<std::filesystem::path> candidates;
#endif

    if (const auto executable = firstExisting(candidates)) {
        application.installed = true;
        application.executable = *executable;
        application.detail =
            "Installed. The active Google identity remains managed by the official "
            "application/system keyring.";
    } else {
        application.detail =
            "Not detected by known install paths. Multi-profile auth selection is "
            "not exposed as a stable external API.";
    }
    return application;
}

}  // namespace routerai
