#include "core/AccountSelector.hpp"

#include <algorithm>

namespace routerai {

namespace {

bool eligible(const Account& account) {
    if (!account.enabled) {
        return false;
    }
    return account.status == AccountStatus::Ready ||
           account.status == AccountStatus::Warning;
}

int statusRank(AccountStatus status) {
    return status == AccountStatus::Ready ? 0 : 1;
}

}  // namespace

std::optional<RoutingCandidate> AccountSelector::select(
    const std::vector<RoutingCandidate>& candidates) const {
    std::vector<RoutingCandidate> eligibleCandidates;
    eligibleCandidates.reserve(candidates.size());

    for (const auto& candidate : candidates) {
        if (eligible(candidate.account)) {
            eligibleCandidates.push_back(candidate);
        }
    }

    if (eligibleCandidates.empty()) {
        return std::nullopt;
    }

    std::sort(
        eligibleCandidates.begin(),
        eligibleCandidates.end(),
        [](const RoutingCandidate& left, const RoutingCandidate& right) {
            const int leftStatus = statusRank(left.account.status);
            const int rightStatus = statusRank(right.account.status);
            if (leftStatus != rightStatus) {
                return leftStatus < rightStatus;
            }

            const bool leftKnown = left.latestUsedPercent.has_value();
            const bool rightKnown = right.latestUsedPercent.has_value();
            if (leftKnown != rightKnown) {
                return leftKnown;
            }

            if (leftKnown && rightKnown &&
                *left.latestUsedPercent != *right.latestUsedPercent) {
                return *left.latestUsedPercent < *right.latestUsedPercent;
            }

            if (left.account.priority != right.account.priority) {
                return left.account.priority > right.account.priority;
            }

            return left.account.id < right.account.id;
        });

    return eligibleCandidates.front();
}

}  // namespace routerai
