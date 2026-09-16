#pragma once

#include <cstdint>

namespace routerai {

class FailurePolicy {
public:
    static std::int64_t cooldownSeconds(int consecutiveFailures);
    static std::int64_t cooldownUntilUnix(
        int consecutiveFailures,
        std::int64_t nowUnix);
};

}  // namespace routerai
