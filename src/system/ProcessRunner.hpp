#pragma once

#include <string>
#include <utility>
#include <vector>

namespace routerai {

struct ProcessResult {
    int exitCode{-1};
    std::string output;
};

class ProcessRunner {
public:
    using Environment = std::vector<std::pair<std::string, std::string>>;

    static int runInteractive(const std::string& command, const Environment& environment = {});
    static ProcessResult runCapture(const std::string& command, const Environment& environment = {});

private:
    static std::string buildShellCommand(const std::string& command, const Environment& environment);
};

}  // namespace routerai
