#pragma once

#include "core/Provider.hpp"

#include <string>

namespace routerai {

class ZaiProvider final : public Provider {
public:
    std::string name() const override;
    Account createPlaceholderAccount(const std::string& accountId) const override;
    Account createAccount(const std::string& accountId, const std::string& mode) const;
    LoginResult login(Account& account, const LoginOptions& options) const override;
    AuthStatus authStatus(const Account& account) const override;
    AccountProfile readProfile(const Account& account) const override;
    QuotaSnapshot readQuota(const Account& account) const override;

    static bool supportsUnifiedRouting(const Account& account);
    static std::string codingBaseUrl();
    static std::string generalBaseUrl();
};

}  // namespace routerai
