#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace routerai {

inline constexpr std::size_t kDefaultRequestLogMaxRows = 10000;
inline constexpr int kDefaultRequestLogMaxAgeDays = 30;

struct RequestLogEntry {
    std::int64_t id{0};
    std::string createdAt;
    std::string groupId;
    std::string accountId;
    std::string provider;
    std::string model;
    long statusCode{0};
    std::int64_t durationMs{0};
    bool streaming{false};
    bool success{false};
    std::string error;
};

struct RequestLogRetentionPolicy {
    std::size_t maxRows{kDefaultRequestLogMaxRows};
    int maxAgeDays{kDefaultRequestLogMaxAgeDays};
};

struct RequestLogMaintenanceResult {
    std::size_t beforeRows{0};
    std::size_t afterRows{0};
    std::size_t removedRows{0};
};

}  // namespace routerai
