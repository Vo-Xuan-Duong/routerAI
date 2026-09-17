#include "desktop/DesktopAccountSwitcher.hpp"

namespace routerai {

DesktopSwitchCapability DesktopAccountSwitcher::capability(
    const std::string& applicationId) const {
    DesktopSwitchCapability result;
    result.applicationId = applicationId;

    if (applicationId == "codex-desktop") {
        result.detail =
            "No supported external Codex Desktop account-switch adapter is registered in this build. "
            "routerAI will only enable switching after OpenAI exposes a stable supported interface.";
        return result;
    }
    if (applicationId == "antigravity-desktop") {
        result.detail =
            "No supported external Antigravity Desktop account-switch adapter is registered in this build. "
            "routerAI will not copy cookies, OAuth tokens, profile databases or OS keyring entries.";
        return result;
    }

    result.detail = "Unknown desktop application.";
    return result;
}

bool DesktopAccountSwitcher::switchAccount(
    const std::string& applicationId,
    const std::string&,
    std::string* detail) const {
    const auto current = capability(applicationId);
    if (detail) *detail = current.detail;
    return false;
}

}  // namespace routerai
