#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace routerai {

struct DesktopApplication {
    std::string id;
    std::string displayName;
    bool installed{false};
    std::filesystem::path executable;
    std::string detail;
};

class DesktopProfileManager {
public:
    std::vector<DesktopApplication> detectApplications() const;
    std::optional<DesktopApplication> findApplication(const std::string& id) const;
    bool launch(const DesktopApplication& application) const;

private:
    static DesktopApplication detectCodex();
    static DesktopApplication detectAntigravity();
};

}  // namespace routerai
