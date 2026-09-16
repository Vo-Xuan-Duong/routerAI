#pragma once

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace routerai {

class DuplexProcess {
public:
    using Environment = std::vector<std::pair<std::string, std::string>>;

    DuplexProcess(
        const std::string& program,
        const std::vector<std::string>& arguments,
        const Environment& environment = {});
    ~DuplexProcess();

    DuplexProcess(const DuplexProcess&) = delete;
    DuplexProcess& operator=(const DuplexProcess&) = delete;
    DuplexProcess(DuplexProcess&&) noexcept;
    DuplexProcess& operator=(DuplexProcess&&) noexcept;

    void writeLine(const std::string& line);
    std::optional<std::string> readLine();
    void closeInput();
    int wait();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace routerai
