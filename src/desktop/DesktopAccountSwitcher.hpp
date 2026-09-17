#pragma once

#include <string>

namespace routerai {

struct DesktopSwitchCapability {
    std::string applicationId;
    bool supported{false};
    std::string detail;
};

class DesktopAccountSwitcher {
public:
    DesktopSwitchCapability capability(const std::string& applicationId) const;
    bool switchAccount(
        const std::string& applicationId,
        const std::string& providerAccountId,
        std::string* detail = nullptr) const;
};

}  // namespace routerai
