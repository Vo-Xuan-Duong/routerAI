#pragma once

#include "core/Provider.hpp"
#include "providers/codex/CodexCli.hpp"

namespace routerai {

class CodexProvider final : public Provider {
public:
    std::string name() const override;
    Account createPlaceholderAccount(const std::string& accountId) const override;
    LoginResult login(Account& account, const LoginOptions& options) const override;
    AuthStatus authStatus(const Account& account) const override;
    AccountProfile readProfile(const Account& account) const override;
    QuotaSnapshot readQuota(const Account& account) const override;

    bool cliInstalled() const;
    std::string cliVersion() const;

private:
    CodexCli cli_;
};

}  // namespace routerai
