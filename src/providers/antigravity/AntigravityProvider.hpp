#pragma once

#include "core/Provider.hpp"
#include "providers/antigravity/AntigravityCli.hpp"

#include <string>

namespace routerai {

class AntigravityProvider final : public Provider {
public:
    std::string name() const override;
    Account createPlaceholderAccount(const std::string& accountId) const override;
    LoginResult login(Account& account, const LoginOptions& options) const override;
    AuthStatus authStatus(const Account& account) const override;
    AccountProfile readProfile(const Account& account) const override;
    QuotaSnapshot readQuota(const Account& account) const override;

    bool cliInstalled() const;
    std::string cliVersion() const;
    int installCli() const;
    static bool supportsUnifiedRouting(const Account& account);

private:
    AntigravityCli cli_;
};

}  // namespace routerai
