#pragma once

#include "core/AccountSelector.hpp"
#include "core/Routing.hpp"
#include "storage/SQLiteDatabase.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace routerai {

struct RoutingDecision {
    RoutingGroup group;
    RoutingCandidate candidate;
};

class RoutingManager {
public:
    explicit RoutingManager(SQLiteDatabase& database);

    void saveGroup(const RoutingGroup& group);
    std::optional<RoutingGroup> findGroup(const std::string& groupId) const;
    std::vector<RoutingGroup> listGroups() const;
    void syncDefaultGroups();

    std::optional<RoutingDecision> select(
        const std::string& groupId,
        std::int64_t nowUnix);
    std::optional<RoutingDecision> select(const std::string& groupId);

    void recordFailure(
        const std::string& accountId,
        const std::string& error,
        std::int64_t nowUnix);
    void recordSuccess(const std::string& accountId);

private:
    SQLiteDatabase& database_;

    std::vector<RoutingCandidate> candidatesFor(const RoutingGroup& group) const;
};

}  // namespace routerai
