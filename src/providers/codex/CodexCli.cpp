#include "providers/codex/CodexCli.hpp"

#include "system/ProcessRunner.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace routerai {

bool CodexCli::isInstalled() const {
    const auto result = ProcessRunner::runCapture("codex --version");
    return result.exitCode == 0;
}

std::string CodexCli::version() const {
    const auto result = ProcessRunner::runCapture("codex --version");
    if (result.exitCode != 0) {
        return {};
    }
    return trim(result.output);
}

int CodexCli::login(const std::filesystem::path& codexHome, bool useBrowser) const {
    std::filesystem::create_directories(codexHome);

    const ProcessRunner::Environment environment{
        {"CODEX_HOME", codexHome.string()}
    };

    const std::string command = useBrowser
        ? "codex login"
        : "codex login --device-auth";

    return ProcessRunner::runInteractive(command, environment);
}

CodexCliStatus CodexCli::status(const std::filesystem::path& codexHome) const {
    CodexCliStatus status;
    status.installed = isInstalled();

    if (!status.installed) {
        status.detail = "Codex CLI is not installed or is not available on PATH";
        return status;
    }

    const ProcessRunner::Environment environment{
        {"CODEX_HOME", codexHome.string()}
    };

    const auto result = ProcessRunner::runCapture("codex login status", environment);
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
