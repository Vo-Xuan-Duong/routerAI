#pragma once

#include <cstdint>
#include <string>

namespace routerai {

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

}  // namespace routerai
