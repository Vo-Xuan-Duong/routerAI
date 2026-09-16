#include "providers/zai/ZaiProvider.hpp"

#include <filesystem>
#include <stdexcept>

namespace routerai {

std::string ZaiProvider::name() const {
    return "zai";
}

Account ZaiProvider::createPlaceholderAccount(const std::string& accountId) const {
    return createAccount(accountId, "general-api");
}

Account ZaiProvider::createAccount(
    const std::string& accountId,
    const std::string& mode) const {
    if (mode != "general-api" && mode != "coding-plan") {
        throw std::runtime_error("Unsupported Z.ai account mode: " + mode);
    }

    Account account;
    account.id = accountId;
    account.provider = name();
    account.providerMode = mode;
    account.displayName = mode == "coding-plan"
        ? "Z.ai Coding Plan"
        : "Z.ai General API";
    account.planType = mode;
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
        "Z.ai uses API-key credentials. Configure the key from routerAI instead of OAuth login."
    };
}

AuthStatus ZaiProvider::authStatus(const Account& account) const {
    return AuthStatus{
        !account.credentialRef.empty(),
        account.credentialRef.empty()
            ? "Z.ai API key is not configured"
            : "Z.ai credential reference is configured"
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
        "Z.ai does not currently document a public machine-readable Coding Plan quota endpoint");
}

bool ZaiProvider::supportsUnifiedRouting(const Account& account) {
    return account.provider == "zai" && account.providerMode == "general-api";
}

std::string ZaiProvider::codingBaseUrl() {
    return "https://api.z.ai/api/coding/paas/v4";
}

std::string ZaiProvider::generalBaseUrl() {
    return "https://api.z.ai/api/paas/v4";
}

}  // namespace routerai
