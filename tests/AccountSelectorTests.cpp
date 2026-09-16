#include "core/AccountSelector.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

routerai::RoutingCandidate candidate(
    std::string id,
    routerai::AccountStatus status,
    int priority,
    std::optional<double> used,
    bool enabled = true,
    std::optional<std::int64_t> cooldownUntilUnix = std::nullopt) {
    routerai::RoutingCandidate value;
    value.account.id = std::move(id);
    value.account.provider = "codex";
    value.account.status = status;
    value.account.priority = priority;
    value.account.enabled = enabled;
    value.account.cooldownUntilUnix = cooldownUntilUnix;
    value.latestUsedPercent = used;
    return value;
}

}  // namespace

int main() {
    try {
        constexpr std::int64_t nowUnix = 1'800'000'000;
        routerai::AccountSelector selector;

        {
            const auto selected = selector.select({
                candidate("warning-low", routerai::AccountStatus::Warning, 100, 10.0),
                candidate("ready-high", routerai::AccountStatus::Ready, 50, 80.0),
            }, nowUnix);
            require(selected.has_value(), "expected a selection");
            require(selected->account.id == "ready-high", "READY must rank before WARNING");
        }

        {
            const auto selected = selector.select({
                candidate("ready-70", routerai::AccountStatus::Ready, 100, 70.0),
                candidate("ready-20", routerai::AccountStatus::Ready, 10, 20.0),
            }, nowUnix);
            require(selected.has_value(), "expected a selection");
            require(selected->account.id == "ready-20", "lower known usage must win within the same status");
        }

        {
            const auto selected = selector.select({
                candidate("unknown", routerai::AccountStatus::Ready, 100, std::nullopt),
                candidate("known", routerai::AccountStatus::Ready, 10, 50.0),
            }, nowUnix);
            require(selected.has_value(), "expected a selection");
            require(selected->account.id == "known", "known usage must rank before unknown usage");
        }

        {
            const auto selected = selector.select({
                candidate("priority-10", routerai::AccountStatus::Ready, 10, 20.0),
                candidate("priority-100", routerai::AccountStatus::Ready, 100, 20.0),
            }, nowUnix);
            require(selected.has_value(), "expected a selection");
            require(selected->account.id == "priority-100", "higher priority must break equal-usage ties");
        }

        {
            const auto selected = selector.select({
                candidate("cooling", routerai::AccountStatus::Ready, 100, 5.0, true, nowUnix + 60),
                candidate("available", routerai::AccountStatus::Ready, 10, 50.0),
            }, nowUnix);
            require(selected.has_value(), "expected an available account");
            require(selected->account.id == "available", "an active cooldown must exclude an account");
        }

        {
            const auto selected = selector.select({
                candidate("expired-cooldown", routerai::AccountStatus::Ready, 100, 5.0, true, nowUnix),
                candidate("available", routerai::AccountStatus::Ready, 10, 50.0),
            }, nowUnix);
            require(selected.has_value(), "expected a selection after cooldown expiry");
            require(selected->account.id == "expired-cooldown", "expired cooldown must not exclude an account");
        }

        {
            const auto selected = selector.select({
                candidate("limited", routerai::AccountStatus::Limited, 100, 5.0),
                candidate("expired", routerai::AccountStatus::AuthExpired, 100, 5.0),
                candidate("disabled", routerai::AccountStatus::Ready, 100, 5.0, false),
            }, nowUnix);
            require(!selected.has_value(), "ineligible accounts must not be selected");
        }

        std::cout << "AccountSelectorTests: OK\n";
    } catch (const std::exception& exception) {
        std::cerr << "AccountSelectorTests: FAILED: " << exception.what() << '\n';
        return 1;
    }

    return 0;
}
