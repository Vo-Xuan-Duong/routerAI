#pragma once

#include <nlohmann/json.hpp>

#include <string>

namespace routerai::openai_compat {

bool wantsStreaming(const nlohmann::json& request);

std::string resolveRoutingGroup(
    const nlohmann::json& request,
    const std::string& headerGroup,
    const std::string& fallbackGroup = "mixed-default");

std::string resolveProviderModelOverride(
    const nlohmann::json& request,
    const std::string& provider);

std::string resolveProviderModel(
    const nlohmann::json& request,
    const std::string& provider);

std::string bufferedChatCompletionSse(const nlohmann::json& response);

}  // namespace routerai::openai_compat
