#pragma once

#include "core/RoutingManager.hpp"
#include "storage/SQLiteDatabase.hpp"

#include <filesystem>
#include <string>

namespace routerai {

struct ConfigTransferResult {
    std::size_t accounts{0};
    std::size_t routingGroups{0};
    std::string detail;
};

class ConfigManager {
public:
    ConfigManager(SQLiteDatabase& database, RoutingManager& routing);

    ConfigTransferResult exportTo(const std::filesystem::path& path) const;
    ConfigTransferResult importFrom(const std::filesystem::path& path);

private:
    SQLiteDatabase& database_;
    RoutingManager& routing_;
};

}  // namespace routerai
