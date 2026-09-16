#pragma once

#include "core/AccountProfile.hpp"
#include "core/Quota.hpp"
#include "system/DuplexProcess.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

namespace routerai {

struct CodexCompletionResult {
    std::string text;
    std::string model;
    std::string threadId;
    std::string turnId;
};

class CodexAppServerClient {
public:
    explicit CodexAppServerClient(const std::filesystem::path& codexHome);

    AccountProfile readAccountProfile();
    QuotaSnapshot readRateLimits();
    CodexCompletionResult runPrompt(
        const std::string& prompt,
        const std::string& model = {},
        const std::string& baseInstructions = {},
        const std::string& developerInstructions = {});

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
    nlohmann::json readMessage();

    static AccountProfile parseAccountProfile(const nlohmann::json& result);
    static QuotaSnapshot parseQuotaSnapshot(const nlohmann::json& result);
};

}  // namespace routerai
