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

struct QuotaHistoryEntry {
    std::int64_t snapshotId{0};
    std::string capturedAt;
    std::string accountId;
    std::optional<bool> ordinaryUsageAllowed;
    std::string providerAccountId;
    std::string limitId;
    std::string limitName;
    std::string model;
    std::string planType;
    std::string reachedType;
    std::string windowName;
    double usedPercent{0.0};
    std::optional<std::int64_t> windowDurationMinutes;
    std::optional<std::int64_t> resetsAtUnix;
};

}  // namespace routerai
