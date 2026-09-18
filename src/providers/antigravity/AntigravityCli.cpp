#include "providers/antigravity/AntigravityCli.hpp"

#include "system/ProcessRunner.hpp"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
#include <utility>

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

std::int64_t parseIso8601(const std::string& value) {
    int y = 0, m = 0, d = 0, h = 0, min = 0, s = 0;
    if (std::sscanf(value.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d", &y, &m, &d, &h, &min, &s) == 6) {
        std::tm tm{};
        tm.tm_year = y - 1900;
        tm.tm_mon = m - 1;
        tm.tm_mday = d;
        tm.tm_hour = h;
        tm.tm_min = min;
        tm.tm_sec = s;
        tm.tm_isdst = 0;
#ifdef _WIN32
        return static_cast<std::int64_t>(_mkgmtime(&tm));
#else
        return static_cast<std::int64_t>(timegm(&tm));
#endif
    }
    return 0;
}

std::string trimCopy(std::string value) {
    const auto notSpace = [](unsigned char ch) { return !std::isspace(ch); };
    const auto first = std::find_if(value.begin(), value.end(), notSpace);
    if (first == value.end()) return {};
    const auto last = std::find_if(value.rbegin(), value.rend(), notSpace).base();
    return std::string(first, last);
}

std::vector<std::string> splitTabs(const std::string& str) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (start < str.size()) {
        const auto pos = str.find('\t', start);
        if (pos == std::string::npos) {
            parts.push_back(trimCopy(str.substr(start)));
            break;
        }
        parts.push_back(trimCopy(str.substr(start, pos - start)));
        start = pos + 1;
    }
    return parts;
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

    std::string lower = prefix;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });

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

int AntigravityCli::install() const {
#ifdef _WIN32
    return ProcessRunner::runInteractive(
        "powershell -NoProfile -ExecutionPolicy Bypass -Command \"irm https://antigravity.google/cli/install.ps1 | iex\"");
#else
    return ProcessRunner::runInteractive(
        "curl -fsSL https://antigravity.google/cli/install.sh | bash");
#endif
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

    const auto result = ProcessRunner::runCapture("agy -p \"/usage\"");
    if (result.exitCode != 0) {
        throw std::runtime_error(
            "Antigravity /usage failed (exit code " +
            std::to_string(result.exitCode) + "): " + trim(result.output));
    }

    return parseQuotaOutput(result.output);
}

QuotaSnapshot AntigravityCli::parseQuotaOutput(const std::string& output) {
    QuotaSnapshot snapshot;
    std::istringstream input(output);
    std::string line;
    std::string previous;
    std::unordered_set<std::string> seen;
    static const std::regex percentRegex(R"(([0-9]+(?:\.[0-9]+)?)\s*%)");
    static const std::regex refreshPattern(
        R"((?:refreshes|resets)\s+in\s+([^\t\r\n]+))",
        std::regex::icase);

    const std::int64_t now = static_cast<std::int64_t>(std::time(nullptr));
    bool anyAllowed = false;

    while (std::getline(input, line)) {
        line = trimCopy(line);
        if (line.empty()) {
            continue;
        }

        // Check for tab-separated output first (agy 1.1.12+ / 1.2+ TSV format)
        if (line.find('\t') != std::string::npos) {
            const auto parts = splitTabs(line);
            int percentCol = -1;
            double percentVal = 0.0;

            for (int i = 0; i < static_cast<int>(parts.size()); ++i) {
                std::smatch match;
                if (std::regex_search(parts[static_cast<std::size_t>(i)], match, percentRegex)) {
                    try {
                        percentVal = std::stod(match[1].str());
                        percentCol = i;
                        break;
                    } catch (...) {}
                }
            }

            if (percentCol >= 0) {
                percentVal = std::clamp(percentVal, 0.0, 100.0);
                std::string model;
                std::string limit;
                if (percentCol >= 2) {
                    model = parts[0];
                    limit = parts[1];
                } else if (percentCol == 1) {
                    limit = parts[0];
                }

                // Strip trailing "Remaining" / "remaining" from limit name
                std::string lowerLimit = limit;
                std::transform(lowerLimit.begin(), lowerLimit.end(), lowerLimit.begin(), [](unsigned char ch) {
                    return static_cast<char>(std::tolower(ch));
                });
                if (lowerLimit.ends_with("remaining")) {
                    limit = trimCopy(limit.substr(0, limit.size() - 9));
                }
                while (!limit.empty() && (limit.back() == '-' || limit.back() == ':' || limit.back() == '|')) {
                    limit.pop_back();
                    limit = trimCopy(limit);
                }

                std::string label = model.empty() ? limit : (limit.empty() ? model : model + " - " + limit);
                if (label.empty()) label = "Antigravity quota";

                if (!seen.insert(label).second) {
                    previous = line;
                    continue;
                }

                double remaining = percentVal;
                // Check if the column or line says "used" instead of remaining
                std::string lowerLine = line;
                std::transform(lowerLine.begin(), lowerLine.end(), lowerLine.begin(), [](unsigned char ch) {
                    return static_cast<char>(std::tolower(ch));
                });
                if (lowerLine.find("used") != std::string::npos && lowerLine.find("remaining") == std::string::npos) {
                    remaining = 100.0 - percentVal;
                }

                QuotaBucket bucket;
                bucket.limitId = label;
                bucket.limitName = label;
                bucket.model = model.empty() ? label : model;
                bucket.planType = "antigravity";
                if (remaining <= 0.0) {
                    bucket.reachedType = "quota_exhausted";
                }

                QuotaWindow window;
                window.name = limit.empty() ? "quota" : limit;
                window.usedPercent = 100.0 - remaining;

                // Check for timestamp or duration in subsequent columns
                if (static_cast<std::size_t>(percentCol + 1) < parts.size()) {
                    const std::string& resetStr = parts[static_cast<std::size_t>(percentCol + 1)];
                    std::int64_t resetsAt = parseIso8601(resetStr);
                    if (resetsAt <= 0) {
                        const std::int64_t seconds = parseDurationSeconds(resetStr);
                        if (seconds > 0) resetsAt = now + seconds;
                    }
                    if (resetsAt > 0) {
                        window.resetsAtUnix = resetsAt;
                        if (resetsAt > now) {
                            window.windowDurationMinutes = (resetsAt - now) / 60;
                        }
                    }
                }

                bucket.windows.push_back(std::move(window));
                snapshot.buckets.push_back(std::move(bucket));
                anyAllowed = anyAllowed || remaining > 0.0;
                previous = line;
                continue;
            }
        }

        // Regular text parsing (legacy or space-delimited formats)
        std::smatch percentMatch;
        if (!std::regex_search(line, percentMatch, percentRegex)) {
            previous = line;
            continue;
        }

        double percentVal = 0.0;
        try {
            percentVal = std::stod(percentMatch[1].str());
        } catch (...) {
            previous = line;
            continue;
        }
        percentVal = std::clamp(percentVal, 0.0, 100.0);

        std::string lowerLine = line;
        std::transform(lowerLine.begin(), lowerLine.end(), lowerLine.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });

        double remaining = percentVal;
        if (lowerLine.find("used") != std::string::npos && lowerLine.find("remaining") == std::string::npos) {
            remaining = 100.0 - percentVal;
        }

        std::string label = labelFromLine(
            line,
            static_cast<std::size_t>(percentMatch.position()),
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
        if (remaining <= 0.0) {
            bucket.reachedType = "quota_exhausted";
        }

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
        previous = line;
    }

    if (snapshot.buckets.empty()) {
        throw std::runtime_error(
            "Antigravity /usage returned data, but routerAI could not parse a quota percentage");
    }

    snapshot.ordinaryUsageAllowed = anyAllowed;
    return snapshot;
}

std::string AntigravityCli::trim(std::string value) {
    return trimCopy(std::move(value));
}

}  // namespace routerai
