#pragma once

#include "core/Account.hpp"
#include "core/Quota.hpp"
#include "core/Routing.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

struct sqlite3;

namespace routerai {

class SQLiteDatabase {
public:
    explicit SQLiteDatabase(std::string path);
    ~SQLiteDatabase();

    SQLiteDatabase(const SQLiteDatabase&) = delete;
    SQLiteDatabase& operator=(const SQLiteDatabase&) = delete;

    void initialize();
    void insertAccount(const Account& account);
    void updateAccount(const Account& account);
    std::optional<Account> findAccount(const std::string& accountId) const;
    std::vector<Account> listAccounts() const;
    std::size_t countAccounts() const;

    void recordQuotaSnapshot(
        const std::string& accountId,
        const QuotaSnapshot& snapshot);
    std::vector<QuotaHistoryEntry> listQuotaHistory(
        const std::string& accountId,
        std::size_t limit = 50) const;

    void saveRoutingGroup(const RoutingGroup& group);
    std::optional<RoutingGroup> findRoutingGroup(const std::string& groupId) const;
    std::vector<RoutingGroup> listRoutingGroups() const;
    void updateRoutingGroupCursor(const std::string& groupId, int lastIndex);

    const std::string& path() const noexcept { return path_; }

private:
    std::string path_;
    sqlite3* db_{nullptr};

    void execute(const char* sql) const;
    bool columnExists(const std::string& table, const std::string& column) const;
};

}  // namespace routerai
