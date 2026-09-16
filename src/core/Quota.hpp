#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace routerai {

struct QuotaWindow {
    std::string name;
    double usedPercent{0.0};
    std::optional<std::int64_t> windowDurationMinutes;
    std::optional<std::int64_t> resetsAtUnix;
};

struct QuotaBucket {
    std::string limitId;
    std::string limitName;
    std::string model;
    std::string planType;
    std::string reachedType;
    std::vector<QuotaWindow> windows;
};

struct QuotaSnapshot {
    std::optional<bool> ordinaryUsageAllowed;
    std::string accountId;
    std::vector<QuotaBucket> buckets;
};

}  // namespace routerai
