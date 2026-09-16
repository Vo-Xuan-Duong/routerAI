#pragma once

#include "core/Account.hpp"
#include "core/AccountProfile.hpp"
#include "core/Quota.hpp"

#include <string>

namespace routerai {

struct LoginOptions {
    bool useBrowser{false};
};

struct LoginResult {
    bool success{false};
    std::string detail;
};

struct AuthStatus {
    bool authenticated{false};
    std::string detail;
};

class Provider {
public:
    virtual ~Provider() = default;

    virtual std::string name() const = 0;
    virtual Account createPlaceholderAccount(const std::string& accountId) const = 0;
    virtual LoginResult login(Account& account, const LoginOptions& options) const = 0;
    virtual AuthStatus authStatus(const Account& account) const = 0;
    virtual AccountProfile readProfile(const Account& account) const = 0;
    virtual QuotaSnapshot readQuota(const Account& account) const = 0;
};

}  // namespace routerai
