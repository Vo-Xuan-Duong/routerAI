#include "providers/codex/CodexAppServerClient.hpp"

#include "providers/codex/CodexCli.hpp"

#include <nlohmann/json.hpp>

#include <stdexcept>
#include <utility>

namespace routerai {

namespace {

std::string optionalString(const nlohmann::json& object, const char* key) {
    if (!object.contains(key) || object.at(key).is_null()) {
        return {};
    }
    if (object.at(key).is_string()) {
        return object.at(key).get<std::string>();
    }
    return object.at(key).dump();
}

std::optional<std::int64_t> optionalInt64(const nlohmann::json& object, const char* key) {
    if (!object.contains(key) || object.at(key).is_null()) {
        return std::nullopt;
    }
    if (!object.at(key).is_number_integer() && !object.at(key).is_number_unsigned()) {
        return std::nullopt;
    }
    return object.at(key).get<std::int64_t>();
}

QuotaWindow parseWindow(const nlohmann::json& value, const std::string& name) {
    QuotaWindow window;
    window.name = name;

    if (value.contains("usedPercent") && value.at("usedPercent").is_number()) {
        window.usedPercent = value.at("usedPercent").get<double>();
    }
    window.windowDurationMinutes = optionalInt64(value, "windowDurationMins");
    window.resetsAtUnix = optionalInt64(value, "resetsAt");
    return window;
}

QuotaBucket parseBucket(const nlohmann::json& value, const std::string& fallbackId) {
    QuotaBucket bucket;
    bucket.limitId = optionalString(value, "limitId");
    if (bucket.limitId.empty()) {
        bucket.limitId = fallbackId;
    }
    bucket.limitName = optionalString(value, "limitName");
    bucket.model = optionalString(value, "normalModelSlug");
    bucket.planType = optionalString(value, "planType");
    bucket.reachedType = optionalString(value, "rateLimitReachedType");

    if (value.contains("primary") && value.at("primary").is_object()) {
        bucket.windows.push_back(parseWindow(value.at("primary"), "primary"));
    }
    if (value.contains("secondary") && value.at("secondary").is_object()) {
        bucket.windows.push_back(parseWindow(value.at("secondary"), "secondary"));
    }

    return bucket;
}

std::string codexProgram() {
    CodexCli cli;
    const auto executable = cli.executablePath();
    if (executable.empty()) {
        throw std::runtime_error("Codex runtime is not installed");
    }
    return executable.string();
}

}  // namespace

CodexAppServerClient::CodexAppServerClient(const std::filesystem::path& codexHome)
    : process_(
          codexProgram(),
          {"app-server", "--listen", "stdio://"},
          {{"CODEX_HOME", codexHome.string()}}) {
    initialize();
}

void CodexAppServerClient::initialize() {
    nlohmann::json params = {
        {"clientInfo",
         {
             {"name", "routerAI"},
             {"title", "routerAI"},
             {"version", "0.6.0"},
         }},
        {"capabilities", {{"experimentalApi", false}}},
    };

    request("initialize", params);
    notify("initialized");
}

nlohmann::json CodexAppServerClient::request(
    const std::string& method,
    const std::optional<nlohmann::json>& params) {
    const std::int64_t requestId = nextRequestId_++;

    nlohmann::json message = {
        {"id", requestId},
        {"method", method},
    };
    if (params) {
        message["params"] = *params;
    }

    process_.writeLine(message.dump());
    return readResponse(requestId);
}

void CodexAppServerClient::notify(
    const std::string& method,
    const std::optional<nlohmann::json>& params) {
    nlohmann::json message = {{"method", method}};
    if (params) {
        message["params"] = *params;
    }
    process_.writeLine(message.dump());
}

nlohmann::json CodexAppServerClient::readMessage() {
    while (true) {
        const auto line = process_.readLine();
        if (!line) {
            throw std::runtime_error("Codex app-server closed unexpectedly");
        }
        if (line->empty()) {
            continue;
        }

        try {
            return nlohmann::json::parse(*line);
        } catch (const nlohmann::json::parse_error& error) {
            throw std::runtime_error(
                "Invalid JSON from Codex app-server: " + std::string(error.what()));
        }
    }
}

nlohmann::json CodexAppServerClient::readResponse(std::int64_t requestId) {
    while (true) {
        const nlohmann::json message = readMessage();

        if (!message.contains("id") || message.at("id").is_null()) {
            continue;
        }

        bool matches = false;
        if (message.at("id").is_number_integer()) {
            matches = message.at("id").get<std::int64_t>() == requestId;
        } else if (message.at("id").is_number_unsigned()) {
            matches = static_cast<std::int64_t>(message.at("id").get<std::uint64_t>()) == requestId;
        }
        if (!matches) {
            continue;
        }

        if (message.contains("error") && !message.at("error").is_null()) {
            throw std::runtime_error(
                "Codex app-server request failed: " + message.at("error").dump());
        }
        if (!message.contains("result")) {
            throw std::runtime_error("Codex app-server response has no result");
        }
        return message.at("result");
    }
}

AccountProfile CodexAppServerClient::readAccountProfile() {
    return parseAccountProfile(request("account/read", nlohmann::json::object()));
}

QuotaSnapshot CodexAppServerClient::readRateLimits() {
    return parseQuotaSnapshot(request("account/rateLimits/read"));
}

CodexCompletionResult CodexAppServerClient::runPrompt(
    const std::string& prompt,
    const std::string& model,
    const std::string& baseInstructions,
    const std::string& developerInstructions) {
    if (prompt.empty()) {
        throw std::runtime_error("Codex prompt cannot be empty");
    }

    nlohmann::json threadParams = {
        {"approvalPolicy", "never"},
        {"sandbox", "read-only"},
        {"ephemeral", true},
    };
    if (!model.empty()) {
        threadParams["model"] = model;
    }
    if (!baseInstructions.empty()) {
        threadParams["baseInstructions"] = baseInstructions;
    }
    if (!developerInstructions.empty()) {
        threadParams["developerInstructions"] = developerInstructions;
    }

    const nlohmann::json threadResult = request("thread/start", threadParams);
    if (!threadResult.contains("thread") || !threadResult.at("thread").is_object()) {
        throw std::runtime_error("Codex thread/start response has no thread");
    }

    CodexCompletionResult result;
    result.threadId = optionalString(threadResult.at("thread"), "id");
    result.model = optionalString(threadResult, "model");
    if (result.threadId.empty()) {
        throw std::runtime_error("Codex thread/start response has no thread id");
    }

    nlohmann::json turnParams = {
        {"threadId", result.threadId},
        {"input",
         nlohmann::json::array({
             {
                 {"type", "text"},
                 {"text", prompt},
                 {"text_elements", nlohmann::json::array()},
             },
         })},
        {"approvalPolicy", "never"},
    };
    if (!model.empty()) {
        turnParams["model"] = model;
    }

    const nlohmann::json turnResult = request("turn/start", turnParams);
    if (!turnResult.contains("turn") || !turnResult.at("turn").is_object()) {
        throw std::runtime_error("Codex turn/start response has no turn");
    }
    result.turnId = optionalString(turnResult.at("turn"), "id");
    if (result.turnId.empty()) {
        throw std::runtime_error("Codex turn/start response has no turn id");
    }

    while (true) {
        const nlohmann::json message = readMessage();
        if (!message.contains("method") || !message.at("method").is_string()) {
            continue;
        }

        const std::string method = message.at("method").get<std::string>();
        const auto paramsIt = message.find("params");
        if (paramsIt == message.end() || !paramsIt->is_object()) {
            continue;
        }
        const nlohmann::json& params = *paramsIt;

        if (method == "item/agentMessage/delta") {
            if (optionalString(params, "turnId") == result.turnId) {
                result.text += optionalString(params, "delta");
            }
            continue;
        }

        if (method == "turn/completed") {
            if (!params.contains("turn") || !params.at("turn").is_object()) {
                continue;
            }
            const auto& turn = params.at("turn");
            if (optionalString(turn, "id") != result.turnId) {
                continue;
            }

            const std::string status = optionalString(turn, "status");
            if (status != "completed") {
                const auto errorIt = turn.find("error");
                const std::string detail =
                    errorIt != turn.end() && !errorIt->is_null()
                        ? errorIt->dump()
                        : status;
                throw std::runtime_error("Codex turn did not complete: " + detail);
            }
            return result;
        }
    }
}

AccountProfile CodexAppServerClient::parseAccountProfile(const nlohmann::json& result) {
    if (!result.is_object()) {
        throw std::runtime_error("Codex account response is not an object");
    }

    AccountProfile profile;
    if (result.contains("requiresOpenaiAuth") && result.at("requiresOpenaiAuth").is_boolean()) {
        profile.requiresOpenaiAuth = result.at("requiresOpenaiAuth").get<bool>();
    }

    const auto account = result.find("account");
    if (account == result.end() || account->is_null()) {
        return profile;
    }
    if (!account->is_object()) {
        throw std::runtime_error("Codex account payload is not an object");
    }

    profile.authType = optionalString(*account, "type");
    if (profile.authType == "chatgpt") {
        profile.email = optionalString(*account, "email");
        profile.planType = optionalString(*account, "planType");
    }
    return profile;
}

QuotaSnapshot CodexAppServerClient::parseQuotaSnapshot(const nlohmann::json& result) {
    if (!result.is_object()) {
        throw std::runtime_error("Codex rate-limit response is not an object");
    }

    QuotaSnapshot snapshot;
    if (result.contains("ordinaryUsageAllowed") && result.at("ordinaryUsageAllowed").is_boolean()) {
        snapshot.ordinaryUsageAllowed = result.at("ordinaryUsageAllowed").get<bool>();
    }
    snapshot.accountId = optionalString(result, "accountId");

    const auto byLimitId = result.find("rateLimitsByLimitId");
    if (byLimitId != result.end() && byLimitId->is_object() && !byLimitId->empty()) {
        for (auto it = byLimitId->begin(); it != byLimitId->end(); ++it) {
            if (it.value().is_object()) {
                snapshot.buckets.push_back(parseBucket(it.value(), it.key()));
            }
        }
    } else {
        const auto legacy = result.find("rateLimits");
        if (legacy != result.end() && legacy->is_object()) {
            snapshot.buckets.push_back(parseBucket(*legacy, "codex"));
        }
    }

    return snapshot;
}

}  // namespace routerai
