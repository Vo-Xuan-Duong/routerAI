#include "providers/zai/ZaiProvider.hpp"

#include <filesystem>
#include <stdexcept>

namespace routerai {

std::string ZaiProvider::name() const {
    return "zai";
}

Account ZaiProvider::createPlaceholderAccount(const std::string& accountId) const {
    Account account;
    account.id = accountId;
    account.provider = name();
    account.displayName = "Z.ai Coding Plan";
    account.planType = "coding-plan";
    account.runtimeHome = (std::filesystem::path(".routerai") /
        "accounts" / accountId).string();
    account.status = AccountStatus::AuthExpired;
    account.priority = 100;
    account.enabled = true;
    return account;
}

LoginResult ZaiProvider::login(Account& account, const LoginOptions&) const {
    account.status = AccountStatus::AuthExpired;
    return LoginResult{
        false,
        "Z.ai account OAuth is not exposed as a documented third-party flow. "
        "Configure a Z.ai API key for the Coding Plan instead."
    };
}

AuthStatus ZaiProvider::authStatus(const Account&) const {
    return AuthStatus{
        false,
        "Z.ai API-key credential storage/validation is not wired yet"
    };
}

AccountProfile ZaiProvider::readProfile(const Account& account) const {
    AccountProfile profile;
    profile.authType = "api_key";
    profile.planType = account.planType;
    return profile;
}

QuotaSnapshot ZaiProvider::readQuota(const Account&) const {
    throw std::runtime_error(
        "Z.ai Coding Plan quota is visible in ZCode, but no documented public "
        "quota endpoint is wired into routerAI yet");
}

std::string ZaiProvider::codingBaseUrl() {
    return "https://api.z.ai/api/coding/paas/v4";
}

std::string ZaiProvider::generalBaseUrl() {
    return "https://api.z.ai/api/paas/v4";
}

}  // namespace routerai
