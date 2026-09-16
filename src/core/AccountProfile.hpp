#pragma once

#include <string>

namespace routerai {

struct AccountProfile {
    std::string authType;
    std::string email;
    std::string planType;
    bool requiresOpenaiAuth{false};
};

}  // namespace routerai
