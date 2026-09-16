#pragma once

#include "core/Account.hpp"

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
        const std::vector<RoutingCandidate>& candidates) const;
};

}  // namespace routerai
