#include "providers/antigravity/AntigravityProvider.hpp"

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

LoginResult AntigravityProvider::login(Account& account, const LoginOptions&) const {
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

AuthStatus AntigravityProvider::authStatus(const Account&) const {
    const auto status = cli_.status();
    return AuthStatus{status.authenticated, status.detail};
}

AccountProfile AntigravityProvider::readProfile(const Account&) const {
    AccountProfile profile;
    profile.authType = "system_keyring";
    profile.planType = "antigravity";
    return profile;
}

QuotaSnapshot AntigravityProvider::readQuota(const Account&) const {
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

bool AntigravityProvider::supportsUnifiedRouting(const Account&) {
    return false;
}

}  // namespace routerai
