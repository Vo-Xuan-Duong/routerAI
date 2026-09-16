#include "providers/codex/CodexProvider.hpp"

#include "providers/codex/CodexAppServerClient.hpp"

#include <filesystem>
#include <stdexcept>

namespace routerai {

std::string CodexProvider::name() const {
    return "codex";
}

Account CodexProvider::createPlaceholderAccount(const std::string& accountId) const {
    Account account;
    account.id = accountId;
    account.provider = name();
    account.displayName = "Codex ChatGPT account";
    account.runtimeHome = (std::filesystem::path(".routerai") /
        "accounts" / accountId / "codex-home").string();
    account.status = AccountStatus::AuthExpired;
    account.priority = 100;
    account.enabled = true;
    return account;
}

LoginResult CodexProvider::login(Account& account, const LoginOptions& options) const {
    if (account.runtimeHome.empty()) {
        account.runtimeHome = (std::filesystem::path(".routerai") /
            "accounts" / account.id / "codex-home").string();
    }

    if (!cli_.isInstalled()) {
        account.status = AccountStatus::Error;
        return LoginResult{
            false,
            "Codex CLI is not installed or is not available on PATH"
        };
    }

    const int exitCode = cli_.login(account.runtimeHome, options.useBrowser);
    const auto status = cli_.status(account.runtimeHome);

    if (exitCode == 0 && status.authenticated) {
        account.status = AccountStatus::Ready;
        return LoginResult{true, status.detail};
    }

    account.status = AccountStatus::AuthExpired;
    if (!status.detail.empty()) {
        return LoginResult{false, status.detail};
    }

    return LoginResult{
        false,
        "Codex login did not complete successfully (exit code " +
            std::to_string(exitCode) + ")"
    };
}

AuthStatus CodexProvider::authStatus(const Account& account) const {
    if (account.runtimeHome.empty()) {
        return AuthStatus{false, "Account has no Codex runtime home"};
    }

    const auto status = cli_.status(account.runtimeHome);
    return AuthStatus{status.authenticated, status.detail};
}

AccountProfile CodexProvider::readProfile(const Account& account) const {
    if (account.runtimeHome.empty()) {
        throw std::runtime_error("Account has no Codex runtime home");
    }
    if (!cli_.isInstalled()) {
        throw std::runtime_error("Codex CLI is not installed or is not available on PATH");
    }

    CodexAppServerClient client(account.runtimeHome);
    return client.readAccountProfile();
}

QuotaSnapshot CodexProvider::readQuota(const Account& account) const {
    if (account.runtimeHome.empty()) {
        throw std::runtime_error("Account has no Codex runtime home");
    }
    if (!cli_.isInstalled()) {
        throw std::runtime_error("Codex CLI is not installed or is not available on PATH");
    }

    const auto auth = cli_.status(account.runtimeHome);
    if (!auth.authenticated) {
        throw std::runtime_error(
            auth.detail.empty() ? "Codex account is not authenticated" : auth.detail);
    }

    CodexAppServerClient client(account.runtimeHome);
    return client.readRateLimits();
}

bool CodexProvider::cliInstalled() const {
    return cli_.isInstalled();
}

std::string CodexProvider::cliVersion() const {
    return cli_.version();
}

}  // namespace routerai
