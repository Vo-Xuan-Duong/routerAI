#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace routerai {

class CredentialStore {
public:
    explicit CredentialStore(
        std::filesystem::path root = std::filesystem::path(".routerai") / "secrets");

    void put(const std::string& reference, const std::string& secret) const;
    std::optional<std::string> get(const std::string& reference) const;
    bool exists(const std::string& reference) const;
    void erase(const std::string& reference) const;

private:
    std::filesystem::path root_;

    std::filesystem::path pathFor(const std::string& reference) const;
};

}  // namespace routerai
