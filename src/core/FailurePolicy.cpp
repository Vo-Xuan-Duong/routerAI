#include "core/FailurePolicy.hpp"

#include <algorithm>

namespace routerai {

std::int64_t FailurePolicy::cooldownSeconds(int consecutiveFailures) {
    if (consecutiveFailures <= 0) {
        return 0;
    }

    constexpr std::int64_t baseSeconds = 30;
    constexpr std::int64_t maxSeconds = 15 * 60;
    const int exponent = std::min(consecutiveFailures - 1, 5);
    return std::min(baseSeconds << exponent, maxSeconds);
}

bool FailurePolicy::isCoolingDown(const Account& account, std::int64_t nowUnix) {
    return account.cooldownUntilUnix.has_value() && *account.cooldownUntilUnix > nowUnix;
}

void FailurePolicy::recordFailure(
    Account& account,
    std::int64_t nowUnix,
    const std::string& error) {
    ++account.consecutiveFailures;
    account.cooldownUntilUnix = nowUnix + cooldownSeconds(account.consecutiveFailures);
    account.lastError = error;

    // Transport/request failures are tracked independently from provider
    // health. READY/WARNING/LIMITED remain reserved for auth/quota/provider
    // state so a temporary network error does not overwrite quota semantics.
}

void FailurePolicy::recordSuccess(Account& account) {
    account.consecutiveFailures = 0;
    account.cooldownUntilUnix.reset();
    account.lastError.clear();
}

}  // namespace routerai
