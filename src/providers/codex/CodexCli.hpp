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
    std::string version() const;

    int login(const std::filesystem::path& codexHome, bool useBrowser) const;
    CodexCliStatus status(const std::filesystem::path& codexHome) const;

private:
    static std::string trim(std::string value);
};

}  // namespace routerai
