#include "system/DuplexProcess.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <thread>
#include <utility>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace routerai {

struct DuplexProcess::Impl {
#ifdef _WIN32
    HANDLE process{nullptr};
    HANDLE thread{nullptr};
    HANDLE stdinWrite{nullptr};
    HANDLE stdoutRead{nullptr};
#else
    pid_t pid{-1};
    FILE* stdinStream{nullptr};
    FILE* stdoutStream{nullptr};
#endif
    bool waited{false};
    int exitCode{-1};
};

namespace {

#ifdef _WIN32

std::string windowsError(const char* context) {
    return std::string(context) + " (Windows error " + std::to_string(GetLastError()) + ")";
}

std::string quoteWindowsArgument(const std::string& argument) {
    if (argument.empty()) {
        return "\"\"";
    }

    if (argument.find_first_of(" \t\n\v\"") == std::string::npos) {
        return argument;
    }

    std::string quoted{"\""};
    std::size_t backslashes = 0;
    for (const char ch : argument) {
        if (ch == '\\') {
            ++backslashes;
            continue;
        }

        if (ch == '"') {
            quoted.append(backslashes * 2 + 1, '\\');
            quoted += '"';
            backslashes = 0;
            continue;
        }

        quoted.append(backslashes, '\\');
        backslashes = 0;
        quoted += ch;
    }

    quoted.append(backslashes * 2, '\\');
    quoted += '"';
    return quoted;
}

std::vector<char> buildWindowsEnvironment(const DuplexProcess::Environment& overrides) {
    std::vector<std::string> entries;
    LPCH environment = GetEnvironmentStringsA();
    if (!environment) {
        throw std::runtime_error(windowsError("GetEnvironmentStringsA failed"));
    }

    for (LPCH current = environment; *current != '\0'; current += std::strlen(current) + 1) {
        entries.emplace_back(current);
    }
    FreeEnvironmentStringsA(environment);

    for (const auto& [key, value] : overrides) {
        entries.erase(
            std::remove_if(
                entries.begin(),
                entries.end(),
                [&](const std::string& entry) {
                    const auto equal = entry.find('=', entry.starts_with('=') ? 1 : 0);
                    if (equal == std::string::npos) {
                        return false;
                    }
                    return _stricmp(entry.substr(0, equal).c_str(), key.c_str()) == 0;
                }),
            entries.end());
        entries.push_back(key + '=' + value);
    }

    std::vector<char> block;
    for (const auto& entry : entries) {
        block.insert(block.end(), entry.begin(), entry.end());
        block.push_back('\0');
    }
    block.push_back('\0');
    return block;
}

#else

int normalizePosixExitStatus(int status) {
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }
    return status;
}

#endif

}  // namespace

DuplexProcess::DuplexProcess(
    const std::string& program,
    const std::vector<std::string>& arguments,
    const Environment& environment)
    : impl_(std::make_unique<Impl>()) {
#ifdef _WIN32
    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;

    HANDLE childStdinRead = nullptr;
    HANDLE childStdoutWrite = nullptr;

    if (!CreatePipe(&childStdinRead, &impl_->stdinWrite, &security, 0)) {
        throw std::runtime_error(windowsError("CreatePipe(stdin) failed"));
    }
    if (!SetHandleInformation(impl_->stdinWrite, HANDLE_FLAG_INHERIT, 0)) {
        CloseHandle(childStdinRead);
        CloseHandle(impl_->stdinWrite);
        impl_->stdinWrite = nullptr;
        throw std::runtime_error(windowsError("SetHandleInformation(stdin) failed"));
    }

    if (!CreatePipe(&impl_->stdoutRead, &childStdoutWrite, &security, 0)) {
        CloseHandle(childStdinRead);
        CloseHandle(impl_->stdinWrite);
        impl_->stdinWrite = nullptr;
        throw std::runtime_error(windowsError("CreatePipe(stdout) failed"));
    }
    if (!SetHandleInformation(impl_->stdoutRead, HANDLE_FLAG_INHERIT, 0)) {
        CloseHandle(childStdinRead);
        CloseHandle(impl_->stdinWrite);
        CloseHandle(impl_->stdoutRead);
        CloseHandle(childStdoutWrite);
        impl_->stdinWrite = nullptr;
        impl_->stdoutRead = nullptr;
        throw std::runtime_error(windowsError("SetHandleInformation(stdout) failed"));
    }

    std::string commandLine = quoteWindowsArgument(program);
    for (const auto& argument : arguments) {
        commandLine += ' ';
        commandLine += quoteWindowsArgument(argument);
    }
    std::vector<char> mutableCommand(commandLine.begin(), commandLine.end());
    mutableCommand.push_back('\0');

    auto environmentBlock = buildWindowsEnvironment(environment);

    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = childStdinRead;
    startup.hStdOutput = childStdoutWrite;
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);

    PROCESS_INFORMATION processInfo{};
    const BOOL created = CreateProcessA(
        nullptr,
        mutableCommand.data(),
        nullptr,
        nullptr,
        TRUE,
        0,
        environmentBlock.data(),
        nullptr,
        &startup,
        &processInfo);

    CloseHandle(childStdinRead);
    CloseHandle(childStdoutWrite);

    if (!created) {
        CloseHandle(impl_->stdinWrite);
        CloseHandle(impl_->stdoutRead);
        impl_->stdinWrite = nullptr;
        impl_->stdoutRead = nullptr;
        throw std::runtime_error(windowsError("CreateProcessA failed"));
    }

    impl_->process = processInfo.hProcess;
    impl_->thread = processInfo.hThread;
#else
    int stdinPipe[2]{};
    int stdoutPipe[2]{};

    if (pipe(stdinPipe) != 0) {
        throw std::runtime_error("pipe(stdin) failed: " + std::string(std::strerror(errno)));
    }
    if (pipe(stdoutPipe) != 0) {
        close(stdinPipe[0]);
        close(stdinPipe[1]);
        throw std::runtime_error("pipe(stdout) failed: " + std::string(std::strerror(errno)));
    }

    const pid_t pid = fork();
    if (pid < 0) {
        close(stdinPipe[0]);
        close(stdinPipe[1]);
        close(stdoutPipe[0]);
        close(stdoutPipe[1]);
        throw std::runtime_error("fork failed: " + std::string(std::strerror(errno)));
    }

    if (pid == 0) {
        dup2(stdinPipe[0], STDIN_FILENO);
        dup2(stdoutPipe[1], STDOUT_FILENO);

        close(stdinPipe[0]);
        close(stdinPipe[1]);
        close(stdoutPipe[0]);
        close(stdoutPipe[1]);

        for (const auto& [key, value] : environment) {
            setenv(key.c_str(), value.c_str(), 1);
        }

        std::vector<char*> argv;
        argv.reserve(arguments.size() + 2);
        argv.push_back(const_cast<char*>(program.c_str()));
        for (const auto& argument : arguments) {
            argv.push_back(const_cast<char*>(argument.c_str()));
        }
        argv.push_back(nullptr);

        execvp(program.c_str(), argv.data());
        std::fprintf(stderr, "execvp(%s) failed: %s\n", program.c_str(), std::strerror(errno));
        _exit(127);
    }

    close(stdinPipe[0]);
    close(stdoutPipe[1]);

    impl_->pid = pid;
    impl_->stdinStream = fdopen(stdinPipe[1], "w");
    impl_->stdoutStream = fdopen(stdoutPipe[0], "r");

    if (!impl_->stdinStream || !impl_->stdoutStream) {
        if (impl_->stdinStream) {
            std::fclose(impl_->stdinStream);
            impl_->stdinStream = nullptr;
        } else {
            close(stdinPipe[1]);
        }
        if (impl_->stdoutStream) {
            std::fclose(impl_->stdoutStream);
            impl_->stdoutStream = nullptr;
        } else {
            close(stdoutPipe[0]);
        }
        kill(pid, SIGTERM);
        waitpid(pid, nullptr, 0);
        impl_->pid = -1;
        throw std::runtime_error("fdopen failed for child process pipes");
    }

    setvbuf(impl_->stdinStream, nullptr, _IOLBF, 0);
#endif
}

DuplexProcess::~DuplexProcess() {
    if (!impl_) {
        return;
    }

    closeInput();

#ifdef _WIN32
    if (impl_->process && !impl_->waited) {
        if (WaitForSingleObject(impl_->process, 300) == WAIT_TIMEOUT) {
            TerminateProcess(impl_->process, 1);
            WaitForSingleObject(impl_->process, 2000);
        }
    }
    if (impl_->stdoutRead) CloseHandle(impl_->stdoutRead);
    if (impl_->thread) CloseHandle(impl_->thread);
    if (impl_->process) CloseHandle(impl_->process);
#else
    if (impl_->stdoutStream) {
        std::fclose(impl_->stdoutStream);
        impl_->stdoutStream = nullptr;
    }
    if (impl_->pid > 0 && !impl_->waited) {
        int status = 0;
        for (int i = 0; i < 30; ++i) {
            const pid_t result = waitpid(impl_->pid, &status, WNOHANG);
            if (result == impl_->pid) {
                impl_->waited = true;
                impl_->exitCode = normalizePosixExitStatus(status);
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if (!impl_->waited) {
            kill(impl_->pid, SIGTERM);
            waitpid(impl_->pid, &status, 0);
            impl_->waited = true;
            impl_->exitCode = normalizePosixExitStatus(status);
        }
    }
#endif
}

DuplexProcess::DuplexProcess(DuplexProcess&&) noexcept = default;
DuplexProcess& DuplexProcess::operator=(DuplexProcess&&) noexcept = default;

void DuplexProcess::writeLine(const std::string& line) {
    if (!impl_) {
        throw std::runtime_error("Process is not initialized");
    }

    const std::string payload = line + '\n';
#ifdef _WIN32
    if (!impl_->stdinWrite) {
        throw std::runtime_error("Child process stdin is closed");
    }

    std::size_t offset = 0;
    while (offset < payload.size()) {
        DWORD written = 0;
        const DWORD remaining = static_cast<DWORD>(payload.size() - offset);
        if (!WriteFile(
                impl_->stdinWrite,
                payload.data() + offset,
                remaining,
                &written,
                nullptr)) {
            throw std::runtime_error(windowsError("WriteFile(child stdin) failed"));
        }
        offset += written;
    }
#else
    if (!impl_->stdinStream) {
        throw std::runtime_error("Child process stdin is closed");
    }
    if (std::fwrite(payload.data(), 1, payload.size(), impl_->stdinStream) != payload.size()) {
        throw std::runtime_error("Failed to write to child process stdin");
    }
    if (std::fflush(impl_->stdinStream) != 0) {
        throw std::runtime_error("Failed to flush child process stdin");
    }
#endif
}

std::optional<std::string> DuplexProcess::readLine() {
    if (!impl_) {
        return std::nullopt;
    }

    std::string line;
#ifdef _WIN32
    if (!impl_->stdoutRead) {
        return std::nullopt;
    }

    char ch = '\0';
    DWORD read = 0;
    while (ReadFile(impl_->stdoutRead, &ch, 1, &read, nullptr) && read == 1) {
        if (ch == '\n') {
            break;
        }
        if (ch != '\r') {
            line += ch;
        }
    }
    if (line.empty() && read == 0) {
        return std::nullopt;
    }
#else
    if (!impl_->stdoutStream) {
        return std::nullopt;
    }

    int ch = 0;
    while ((ch = std::fgetc(impl_->stdoutStream)) != EOF) {
        if (ch == '\n') {
            break;
        }
        if (ch != '\r') {
            line += static_cast<char>(ch);
        }
    }
    if (line.empty() && ch == EOF) {
        return std::nullopt;
    }
#endif
    return line;
}

void DuplexProcess::closeInput() {
    if (!impl_) {
        return;
    }
#ifdef _WIN32
    if (impl_->stdinWrite) {
        CloseHandle(impl_->stdinWrite);
        impl_->stdinWrite = nullptr;
    }
#else
    if (impl_->stdinStream) {
        std::fclose(impl_->stdinStream);
        impl_->stdinStream = nullptr;
    }
#endif
}

int DuplexProcess::wait() {
    if (!impl_) {
        return -1;
    }
    if (impl_->waited) {
        return impl_->exitCode;
    }

    closeInput();
#ifdef _WIN32
    if (!impl_->process) {
        return -1;
    }
    WaitForSingleObject(impl_->process, INFINITE);
    DWORD code = 0;
    if (!GetExitCodeProcess(impl_->process, &code)) {
        throw std::runtime_error(windowsError("GetExitCodeProcess failed"));
    }
    impl_->exitCode = static_cast<int>(code);
#else
    if (impl_->pid <= 0) {
        return -1;
    }
    int status = 0;
    if (waitpid(impl_->pid, &status, 0) < 0) {
        throw std::runtime_error("waitpid failed: " + std::string(std::strerror(errno)));
    }
    impl_->exitCode = normalizePosixExitStatus(status);
#endif
    impl_->waited = true;
    return impl_->exitCode;
}

}  // namespace routerai
