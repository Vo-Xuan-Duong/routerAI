#pragma once

#include "core/Account.hpp"

#include <cstdint>
#include <string>

namespace routerai {

class FailurePolicy {
public:
    static std::int64_t cooldownSeconds(int consecutiveFailures);
    static bool isCoolingDown(const Account& account, std::int64_t nowUnix);
    static void recordFailure(
        Account& account,
        std::int64_t nowUnix,
        const std::string& error);
    static void recordSuccess(Account& account);
};

}  // namespace routerai
