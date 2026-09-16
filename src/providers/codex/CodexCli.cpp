#include "providers/codex/CodexCli.hpp"

#include "system/ProcessRunner.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>

namespace routerai {

namespace {

std::string quoteCommandArgument(const std::string& value) {
#ifdef _WIN32
    std::string escaped;
    escaped.reserve(value.size());
    for (const char ch : value) {
        if (ch == '"') {
            escaped += "\\\"";
        } else {
            escaped += ch;
        }
    }
    return "\"" + escaped + "\"";
#else
    std::string quoted = "'";
    for (const char ch : value) {
        if (ch == '\'') {
            quoted += "'\\''";
        } else {
            quoted += ch;
        }
    }
    quoted += "'";
    return quoted;
#endif
}

}  // namespace

std::filesystem::path CodexCli::managedRoot() {
    return std::filesystem::path(".routerai") / "runtime" / "codex";
}

std::filesystem::path CodexCli::managedBinDirectory() {
    return managedRoot() / "bin";
}

std::filesystem::path CodexCli::managedExecutablePath() {
#ifdef _WIN32
    return managedBinDirectory() / "codex.exe";
#else
    return managedBinDirectory() / "codex";
#endif
}

std::filesystem::path CodexCli::managedInstallerHome() {
    return managedRoot() / "installer-home";
}

std::string CodexCli::commandFor(const std::filesystem::path& executable) {
    return quoteCommandArgument(executable.string());
}

bool CodexCli::executableWorks(const std::filesystem::path& executable) {
    if (executable.empty()) {
        return false;
    }

    const auto result = ProcessRunner::runCapture(commandFor(executable) + " --version");
    return result.exitCode == 0;
}

std::filesystem::path CodexCli::executablePath() const {
    const auto managed = managedExecutablePath();
    if (std::filesystem::exists(managed) && executableWorks(managed)) {
        return std::filesystem::absolute(managed);
    }

    const std::filesystem::path systemExecutable{"codex"};
    if (executableWorks(systemExecutable)) {
        return systemExecutable;
    }

    return {};
}

bool CodexCli::isInstalled() const {
    return !executablePath().empty();
}

bool CodexCli::installManaged() const {
    const auto root = std::filesystem::absolute(managedRoot());
    const auto binDirectory = std::filesystem::absolute(managedBinDirectory());
    const auto installerHome = std::filesystem::absolute(managedInstallerHome());

    std::filesystem::create_directories(root);
    std::filesystem::create_directories(binDirectory);
    std::filesystem::create_directories(installerHome);

    const ProcessRunner::Environment environment{
        {"CODEX_HOME", installerHome.string()},
        {"CODEX_INSTALL_DIR", binDirectory.string()},
        {"CODEX_NON_INTERACTIVE", "true"},
    };

#ifdef _WIN32
    const std::string command =
        "powershell -NoProfile -ExecutionPolicy Bypass -Command "
        "\"Invoke-RestMethod https://chatgpt.com/codex/install.ps1 | Invoke-Expression\"";
#else
    const std::string command =
        "curl -fsSL https://chatgpt.com/codex/install.sh | sh";
#endif

    const int exitCode = ProcessRunner::runInteractive(command, environment);
    return exitCode == 0 && executableWorks(managedExecutablePath());
}

bool CodexCli::ensureInstalled() const {
    return isInstalled() || installManaged();
}

std::string CodexCli::version() const {
    const auto executable = executablePath();
    if (executable.empty()) {
        return {};
    }

    const auto result = ProcessRunner::runCapture(commandFor(executable) + " --version");
    if (result.exitCode != 0) {
        return {};
    }
    return trim(result.output);
}

std::string CodexCli::installationSource() const {
    const auto executable = executablePath();
    if (executable.empty()) {
        return "missing";
    }

    const auto managed = std::filesystem::absolute(managedExecutablePath());
    if (executable.is_absolute() && executable == managed) {
        return "routerAI managed";
    }
    return "system PATH";
}

int CodexCli::login(const std::filesystem::path& codexHome, bool useBrowser) const {
    const auto executable = executablePath();
    if (executable.empty()) {
        return -1;
    }

    std::filesystem::create_directories(codexHome);

    const ProcessRunner::Environment environment{
        {"CODEX_HOME", codexHome.string()}
    };

    const std::string command = commandFor(executable) +
        (useBrowser ? " login" : " login --device-auth");

    return ProcessRunner::runInteractive(command, environment);
}

CodexCliStatus CodexCli::status(const std::filesystem::path& codexHome) const {
    CodexCliStatus status;
    const auto executable = executablePath();
    status.installed = !executable.empty();

    if (!status.installed) {
        status.detail = "Codex runtime is not installed";
        return status;
    }

    const ProcessRunner::Environment environment{
        {"CODEX_HOME", codexHome.string()}
    };

    const auto result = ProcessRunner::runCapture(
        commandFor(executable) + " login status",
        environment);
    status.exitCode = result.exitCode;
    status.detail = trim(result.output);
    status.authenticated = result.exitCode == 0 &&
        status.detail.find("Logged in using ChatGPT") != std::string::npos;

    return status;
}

std::string CodexCli::trim(std::string value) {
    const auto notSpace = [](unsigned char ch) {
        return !std::isspace(ch);
    };

    const auto first = std::find_if(value.begin(), value.end(), notSpace);
    if (first == value.end()) {
        return {};
    }

    const auto last = std::find_if(value.rbegin(), value.rend(), notSpace).base();
    return std::string(first, last);
}

}  // namespace routerai
