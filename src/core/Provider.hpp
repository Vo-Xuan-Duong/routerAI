#pragma once

#include "core/Account.hpp"

#include <string>

namespace routerai {

class Provider {
public:
    virtual ~Provider() = default;

    virtual std::string name() const = 0;
    virtual Account createPlaceholderAccount(const std::string& accountId) const = 0;
};

}  // namespace routerai
