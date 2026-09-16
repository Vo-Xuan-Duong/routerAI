#pragma once

#include <string>

namespace routerai {

enum class AccountStatus {
    Ready,
    Warning,
    Limited,
    AuthExpired,
    Disabled,
    Error
};

inline std::string toString(AccountStatus status) {
    switch (status) {
        case AccountStatus::Ready: return "READY";
        case AccountStatus::Warning: return "WARNING";
        case AccountStatus::Limited: return "LIMITED";
        case AccountStatus::AuthExpired: return "AUTH_EXPIRED";
        case AccountStatus::Disabled: return "DISABLED";
        case AccountStatus::Error: return "ERROR";
    }
    return "ERROR";
}

struct Account {
    std::string id;
    std::string provider;
    std::string displayName;
    std::string email;
    std::string planType;
    std::string runtimeHome;
    AccountStatus status{AccountStatus::Ready};
    int priority{100};
    bool enabled{true};
};

}  // namespace routerai
