#include "providers/antigravity/AntigravityProvider.hpp"

#include <stdexcept>

namespace routerai {

std::string AntigravityProvider::name() const {
    return "antigravity";
}

Account AntigravityProvider::createPlaceholderAccount(const std::string& accountId) const {
    Account account;
    account.id = accountId;
    account.provider = name();
    account.providerMode = "consumer-cli";
    account.displayName = "Antigravity active session";
    account.planType = "consumer-cli";
    account.runtimeHome = "system-keyring";
    account.status = AccountStatus::AuthExpired;
    account.priority = 100;
    account.enabled = true;
    return account;
}

Account AntigravityProvider::createApiProjectAccount(const std::string& accountId) const {
    Account account;
    account.id = accountId;
    account.provider = name();
    account.providerMode = "api-project";
    account.displayName = "Antigravity API project";
    account.planType = "gemini-api";
    account.runtimeHome = "gemini-interactions-api";
    account.status = AccountStatus::AuthExpired;
    account.priority = 100;
    account.enabled = true;
    return account;
}

LoginResult AntigravityProvider::login(Account& account, const LoginOptions&) const {
    if (account.providerMode == "api-project") {
        account.status = AccountStatus::AuthExpired;
        return LoginResult{
            false,
            "Antigravity API projects use a Gemini API key instead of interactive CLI login."
        };
    }

    if (!cli_.isInstalled()) {
        account.status = AccountStatus::Error;
        return LoginResult{
            false,
            "Antigravity CLI (`agy`) is not installed. Use Add Provider to run the official Google installer."
        };
    }

    const int exitCode = cli_.login();
    const auto status = cli_.status();
    account.status = status.authenticated
        ? AccountStatus::Ready
        : AccountStatus::AuthExpired;

    return LoginResult{
        exitCode == 0 && status.authenticated,
        status.detail.empty()
            ? "Antigravity login/status exited with code " + std::to_string(exitCode)
            : status.detail,
    };
}

AuthStatus AntigravityProvider::authStatus(const Account& account) const {
    if (account.providerMode == "api-project") {
        return AuthStatus{
            false,
            "Antigravity API project authentication is managed by routerAI's credential store"
        };
    }

    const auto status = cli_.status();
    return AuthStatus{status.authenticated, status.detail};
}

AccountProfile AntigravityProvider::readProfile(const Account& account) const {
    AccountProfile profile;
    if (account.providerMode == "api-project") {
        profile.authType = "api_key";
        profile.planType = "gemini-api";
        return profile;
    }

    profile.authType = "system_keyring";
    profile.planType = "antigravity";
    return profile;
}

QuotaSnapshot AntigravityProvider::readQuota(const Account& account) const {
    if (account.providerMode == "api-project") {
        throw std::runtime_error(
            "Antigravity API-project quota is project-level and is not exposed through the consumer /usage adapter");
    }
    return cli_.readQuota();
}

bool AntigravityProvider::cliInstalled() const {
    return cli_.isInstalled();
}

std::string AntigravityProvider::cliVersion() const {
    return cli_.version();
}

int AntigravityProvider::installCli() const {
    return cli_.install();
}

bool AntigravityProvider::supportsUnifiedRouting(const Account& account) {
    return account.providerMode == "api-project";
}

}  // namespace routerai
