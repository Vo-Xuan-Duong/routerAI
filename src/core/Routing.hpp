#pragma once

#include <string>
#include <vector>

namespace routerai {

enum class RoutingStrategy {
    HealthFirst,
    LeastUsed,
    Priority,
    RoundRobin,
    Manual
};

inline std::string toString(RoutingStrategy strategy) {
    switch (strategy) {
        case RoutingStrategy::HealthFirst: return "HEALTH_FIRST";
        case RoutingStrategy::LeastUsed: return "LEAST_USED";
        case RoutingStrategy::Priority: return "PRIORITY";
        case RoutingStrategy::RoundRobin: return "ROUND_ROBIN";
        case RoutingStrategy::Manual: return "MANUAL";
    }
    return "HEALTH_FIRST";
}

struct RoutingGroup {
    std::string id;
    std::string displayName;
    RoutingStrategy strategy{RoutingStrategy::HealthFirst};
    bool enabled{true};
    std::string manualAccountId;
    int lastIndex{-1};
    std::vector<std::string> accountIds;
};

}  // namespace routerai
