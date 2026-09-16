#include "core/FailurePolicy.hpp"

#include <algorithm>

namespace routerai {

std::int64_t FailurePolicy::cooldownSeconds(int consecutiveFailures) {
    if (consecutiveFailures <= 0) {
        return 0;
    }

    constexpr std::int64_t baseSeconds = 30;
    constexpr std::int64_t maxSeconds = 15 * 60;

    const int exponent = std::min(consecutiveFailures - 1, 10);
    std::int64_t seconds = baseSeconds;
    for (int i = 0; i < exponent && seconds < maxSeconds; ++i) {
        seconds = std::min(seconds * 2, maxSeconds);
    }
    return seconds;
}

std::int64_t FailurePolicy::cooldownUntilUnix(
    int consecutiveFailures,
    std::int64_t nowUnix) {
    return nowUnix + cooldownSeconds(consecutiveFailures);
}

}  // namespace routerai
