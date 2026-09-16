#pragma once

#include "core/Quota.hpp"

#include <string>

namespace routerai {

struct AntigravityCliStatus {
    bool installed{false};
    bool authenticated{false};
    int exitCode{-1};
    std::string detail;
};

class AntigravityCli {
public:
    bool isInstalled() const;
    std::string version() const;
    int install() const;
    int login() const;
    AntigravityCliStatus status() const;
    QuotaSnapshot readQuota() const;

private:
    static std::string trim(std::string value);
};

}  // namespace routerai
