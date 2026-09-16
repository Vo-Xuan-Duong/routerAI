#include "system/ProcessRunner.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <stdexcept>

#ifndef _WIN32
#include <sys/wait.h>
#endif

namespace routerai {

namespace {

std::string quotePosix(const std::string& value) {
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
}

#ifdef _WIN32
std::string quoteWindowsEnvValue(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (const char ch : value) {
        if (ch == '"') {
            escaped += "\"";
        } else {
            escaped += ch;
        }
    }
    return escaped;
}
#endif

int normalizeExitCode(int rawCode) {
    if (rawCode < 0) {
        return rawCode;
    }
#ifdef _WIN32
    return rawCode;
#else
    if (WIFEXITED(rawCode)) {
        return WEXITSTATUS(rawCode);
    }
    if (WIFSIGNALED(rawCode)) {
        return 128 + WTERMSIG(rawCode);
    }
    return rawCode;
#endif
}

}  // namespace

std::string ProcessRunner::buildShellCommand(
    const std::string& command,
    const Environment& environment) {
    if (environment.empty()) {
        return command;
    }

    std::ostringstream shell;
#ifdef _WIN32
    for (const auto& [key, value] : environment) {
        shell << "set \"" << key << '=' << quoteWindowsEnvValue(value) << "\" && ";
    }
    shell << command;
#else
    for (const auto& [key, value] : environment) {
        shell << key << '=' << quotePosix(value) << ' ';
    }
    shell << command;
#endif
    return shell.str();
}

int ProcessRunner::runInteractive(
    const std::string& command,
    const Environment& environment) {
    const std::string shellCommand = buildShellCommand(command, environment);
    return normalizeExitCode(std::system(shellCommand.c_str()));
}

ProcessResult ProcessRunner::runCapture(
    const std::string& command,
    const Environment& environment) {
    const std::string shellCommand = buildShellCommand(command + " 2>&1", environment);

#ifdef _WIN32
    FILE* pipe = _popen(shellCommand.c_str(), "r");
#else
    FILE* pipe = popen(shellCommand.c_str(), "r");
#endif
    if (!pipe) {
        throw std::runtime_error("Failed to start process: " + command);
    }

    std::array<char, 4096> buffer{};
    std::string output;
    while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        output += buffer.data();
    }

#ifdef _WIN32
    const int rawCode = _pclose(pipe);
#else
    const int rawCode = pclose(pipe);
#endif

    return ProcessResult{normalizeExitCode(rawCode), std::move(output)};
}

}  // namespace routerai
