#pragma once

#include "core/Quota.hpp"
#include "system/DuplexProcess.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

#include <nlohmann/json_fwd.hpp>

namespace routerai {

class CodexAppServerClient {
public:
    explicit CodexAppServerClient(const std::filesystem::path& codexHome);

    QuotaSnapshot readRateLimits();

private:
    DuplexProcess process_;
    std::int64_t nextRequestId_{1};

    void initialize();
    nlohmann::json request(
        const std::string& method,
        const std::optional<nlohmann::json>& params = std::nullopt);
    void notify(
        const std::string& method,
        const std::optional<nlohmann::json>& params = std::nullopt);
    nlohmann::json readResponse(std::int64_t requestId);

    static QuotaSnapshot parseQuotaSnapshot(const nlohmann::json& result);
};

}  // namespace routerai
