#pragma once

#include <filesystem>
#include <string>

namespace routerai {

struct CodexCliStatus {
    bool installed{false};
    bool authenticated{false};
    int exitCode{-1};
    std::string detail;
};

class CodexCli {
public:
    bool isInstalled() const;
    bool installManaged() const;
    bool ensureInstalled() const;

    std::filesystem::path executablePath() const;
    std::string version() const;
    std::string installationSource() const;

    int login(const std::filesystem::path& codexHome, bool useBrowser) const;
    CodexCliStatus status(const std::filesystem::path& codexHome) const;

private:
    static std::filesystem::path managedRoot();
    static std::filesystem::path managedBinDirectory();
    static std::filesystem::path managedExecutablePath();
    static std::filesystem::path managedInstallerHome();

    static bool executableWorks(const std::filesystem::path& executable);
    static std::string commandFor(const std::filesystem::path& executable);
    static std::string trim(std::string value);
};

}  // namespace routerai
