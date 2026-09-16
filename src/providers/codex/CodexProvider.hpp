#pragma once

#include "core/Provider.hpp"

namespace routerai {

class CodexProvider final : public Provider {
public:
    std::string name() const override;
    Account createPlaceholderAccount(const std::string& accountId) const override;
};

}  // namespace routerai
