#pragma once

#include "core/Account.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace routerai {

struct RoutingCandidate {
    Account account;
    std::optional<double> latestUsedPercent;
};

class AccountSelector {
public:
    std::optional<RoutingCandidate> select(
        const std::vector<RoutingCandidate>& candidates,
        std::int64_t nowUnix) const;
};

}  // namespace routerai
