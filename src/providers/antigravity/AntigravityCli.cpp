#include "providers/antigravity/AntigravityCli.hpp"

#include "system/ProcessRunner.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <ctime>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace routerai {

namespace {

std::int64_t parseDurationSeconds(const std::string& value) {
    static const std::regex part(R"((\d+)\s*([dhms]))", std::regex::icase);
    std::int64_t seconds = 0;
    for (std::sregex_iterator it(value.begin(), value.end(), part), end; it != end; ++it) {
        const std::int64_t amount = std::stoll((*it)[1].str());
        const char unit = static_cast<char>(std::tolower((*it)[2].str()[0]));
        if (unit == 'd') seconds += amount * 24 * 60 * 60;
        if (unit == 'h') seconds += amount * 60 * 60;
        if (unit == 'm') seconds += amount * 60;
        if (unit == 's') seconds += amount;
    }
    return seconds;
}

std::string trimCopy(std::string value) {
    const auto notSpace = [](unsigned char ch) { return !std::isspace(ch); };
    const auto first = std::find_if(value.begin(), value.end(), notSpace);
    if (first == value.end()) return {};
    const auto last = std::find_if(value.rbegin(), value.rend(), notSpace).base();
    return std::string(first, last);
}

std::string labelFromLine(
    const std::string& line,
    std::size_t percentPosition,
    const std::string& previous) {
    std::string prefix = trimCopy(line.substr(0, percentPosition));
    while (!prefix.empty() &&
           (prefix.back() == '-' || prefix.back() == ':' || prefix.back() == '|' || prefix.back() == '\t')) {
        prefix.pop_back();
        prefix = trimCopy(prefix);
    }

    const auto lower = [&] {
        std::string value = prefix;
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        return value;
    }();

    if (prefix.empty() || lower == "remaining" || lower.ends_with("remaining")) {
        return previous;
    }
    return prefix;
}

}  // namespace

bool AntigravityCli::isInstalled() const {
    const auto result = ProcessRunner::runCapture("agy --version");
    return result.exitCode == 0;
}

std::string AntigravityCli::version() const {
    const auto result = ProcessRunner::runCapture("agy --version");
    return result.exitCode == 0 ? trim(result.output) : std::string{};
}

int AntigravityCli::login() const {
    // /usage is a read-only print-mode command. If there is no active keyring
    // session, the official CLI owns the Google Sign-In flow and browser auth.
    return ProcessRunner::runInteractive("agy -p \"/usage\"");
}

AntigravityCliStatus AntigravityCli::status() const {
    AntigravityCliStatus status;
    status.installed = isInstalled();
    if (!status.installed) {
        status.detail = "Antigravity CLI (`agy`) is not installed or is not available on PATH";
        return status;
    }

    const auto result = ProcessRunner::runCapture("agy -p \"/usage\"");
    status.exitCode = result.exitCode;
    status.detail = trim(result.output);
    status.authenticated = result.exitCode == 0 && !status.detail.empty();
    return status;
}

QuotaSnapshot AntigravityCli::readQuota() const {
    if (!isInstalled()) {
        throw std::runtime_error("Antigravity CLI (`agy`) is not installed");
    }

    // Text output is intentionally preferred here. agy has supported a stable
    // tabular /usage print-mode surface since 1.1.12, while some releases have
    // had malformed raw-newline JSON output. This parser accepts both the
    // human-readable 'N% remaining' form and tab-separated records.
    const auto result = ProcessRunner::runCapture("agy -p \"/usage\"");
    if (result.exitCode != 0) {
        throw std::runtime_error(
            "Antigravity /usage failed (exit code " +
            std::to_string(result.exitCode) + "): " + trim(result.output));
    }

    QuotaSnapshot snapshot;
    std::istringstream input(result.output);
    std::string line;
    std::string previous;
    std::unordered_set<std::string> seen;
    static const std::regex remainingPattern(
        R"(([0-9]+(?:\.[0-9]+)?)\s*%\s*remaining)",
        std::regex::icase);
    static const std::regex refreshPattern(
        R"((?:refreshes|resets)\s+in\s+([^\t\r\n]+))",
        std::regex::icase);

    const std::int64_t now = static_cast<std::int64_t>(std::time(nullptr));
    bool anyAllowed = false;
    bool anyBlocked = false;

    while (std::getline(input, line)) {
        line = trimCopy(line);
        if (line.empty()) {
            continue;
        }

        std::smatch remainingMatch;
        if (!std::regex_search(line, remainingMatch, remainingPattern)) {
            previous = line;
            continue;
        }

        double remaining = 0.0;
        try {
            remaining = std::stod(remainingMatch[1].str());
        } catch (...) {
            previous = line;
            continue;
        }
        remaining = std::clamp(remaining, 0.0, 100.0);

        std::string label = labelFromLine(
            line,
            static_cast<std::size_t>(remainingMatch.position()),
            previous);
        if (label.empty()) {
            label = "Antigravity quota";
        }
        if (!seen.insert(label).second) {
            previous = line;
            continue;
        }

        QuotaBucket bucket;
        bucket.limitId = label;
        bucket.limitName = label;
        bucket.model = label;
        bucket.planType = "antigravity";

        QuotaWindow window;
        window.name = "quota";
        window.usedPercent = 100.0 - remaining;

        std::smatch refreshMatch;
        if (std::regex_search(line, refreshMatch, refreshPattern)) {
            const std::int64_t seconds = parseDurationSeconds(refreshMatch[1].str());
            if (seconds > 0) {
                window.resetsAtUnix = now + seconds;
            }
        }

        bucket.windows.push_back(std::move(window));
        snapshot.buckets.push_back(std::move(bucket));
        anyAllowed = anyAllowed || remaining > 0.0;
        anyBlocked = anyBlocked || remaining <= 0.0;
        previous = line;
    }

    if (snapshot.buckets.empty()) {
        throw std::runtime_error(
            "Antigravity /usage returned data, but routerAI could not parse a quota percentage");
    }

    snapshot.ordinaryUsageAllowed = anyAllowed && !anyBlocked;
    return snapshot;
}

std::string AntigravityCli::trim(std::string value) {
    return trimCopy(std::move(value));
}

}  // namespace routerai
