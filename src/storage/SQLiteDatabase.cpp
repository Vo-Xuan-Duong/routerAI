#include "storage/SQLiteDatabase.hpp"

#include <sqlite3.h>

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace routerai {

namespace {

AccountStatus statusFromString(const std::string& value) {
    if (value == "READY") return AccountStatus::Ready;
    if (value == "WARNING") return AccountStatus::Warning;
    if (value == "LIMITED") return AccountStatus::Limited;
    if (value == "AUTH_EXPIRED") return AccountStatus::AuthExpired;
    if (value == "DISABLED") return AccountStatus::Disabled;
    return AccountStatus::Error;
}

RoutingStrategy routingStrategyFromString(const std::string& value) {
    if (value == "LEAST_USED") return RoutingStrategy::LeastUsed;
    if (value == "PRIORITY") return RoutingStrategy::Priority;
    if (value == "ROUND_ROBIN") return RoutingStrategy::RoundRobin;
    if (value == "MANUAL") return RoutingStrategy::Manual;
    return RoutingStrategy::HealthFirst;
}

void checkSqlite(int rc, sqlite3* db, const char* context) {
    if (rc != SQLITE_OK && rc != SQLITE_DONE && rc != SQLITE_ROW) {
        throw std::runtime_error(std::string(context) + ": " + sqlite3_errmsg(db));
    }
}

std::string columnText(sqlite3_stmt* stmt, int index) {
    const auto* value = sqlite3_column_text(stmt, index);
    return value ? reinterpret_cast<const char*>(value) : std::string{};
}

std::optional<std::int64_t> columnOptionalInt64(sqlite3_stmt* stmt, int index) {
    if (sqlite3_column_type(stmt, index) == SQLITE_NULL) {
        return std::nullopt;
    }
    return sqlite3_column_int64(stmt, index);
}

std::optional<bool> columnOptionalBool(sqlite3_stmt* stmt, int index) {
    if (sqlite3_column_type(stmt, index) == SQLITE_NULL) {
        return std::nullopt;
    }
    return sqlite3_column_int(stmt, index) != 0;
}

void bindOptionalInt64(
    sqlite3_stmt* stmt,
    int index,
    const std::optional<std::int64_t>& value) {
    if (value) {
        sqlite3_bind_int64(stmt, index, *value);
    } else {
        sqlite3_bind_null(stmt, index);
    }
}

void bindOptionalBool(
    sqlite3_stmt* stmt,
    int index,
    const std::optional<bool>& value) {
    if (value) {
        sqlite3_bind_int(stmt, index, *value ? 1 : 0);
    } else {
        sqlite3_bind_null(stmt, index);
    }
}

Account readAccount(sqlite3_stmt* stmt) {
    Account account;
    account.id = columnText(stmt, 0);
    account.provider = columnText(stmt, 1);
    account.providerMode = columnText(stmt, 2);
    account.displayName = columnText(stmt, 3);
    account.email = columnText(stmt, 4);
    account.planType = columnText(stmt, 5);
    account.runtimeHome = columnText(stmt, 6);
    account.credentialRef = columnText(stmt, 7);
    account.status = statusFromString(columnText(stmt, 8));
    account.priority = sqlite3_column_int(stmt, 9);
    account.enabled = sqlite3_column_int(stmt, 10) != 0;
    account.consecutiveFailures = sqlite3_column_int(stmt, 11);
    account.cooldownUntilUnix = columnOptionalInt64(stmt, 12);
    account.lastError = columnText(stmt, 13);
    return account;
}

constexpr const char* accountSelectColumns =
    "id, provider, provider_mode, display_name, email, plan_type, runtime_home, "
    "credential_ref, status, priority, enabled, consecutive_failures, cooldown_until_unix, last_error";

}  // namespace

SQLiteDatabase::SQLiteDatabase(std::string path) : path_(std::move(path)) {
    const int rc = sqlite3_open(path_.c_str(), &db_);
    if (rc != SQLITE_OK) {
        const std::string message = db_ ? sqlite3_errmsg(db_) : "unknown SQLite error";
        if (db_) {
            sqlite3_close(db_);
            db_ = nullptr;
        }
        throw std::runtime_error("Failed to open database: " + message);
    }
}

SQLiteDatabase::~SQLiteDatabase() {
    if (db_) {
        sqlite3_close(db_);
    }
}

void SQLiteDatabase::execute(const char* sql) const {
    char* error = nullptr;
    const int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &error);
    if (rc != SQLITE_OK) {
        const std::string message = error ? error : sqlite3_errmsg(db_);
        sqlite3_free(error);
        throw std::runtime_error("SQLite execution failed: " + message);
    }
}

bool SQLiteDatabase::columnExists(const std::string& table, const std::string& column) const {
    const std::string sql = "PRAGMA table_info(" + table + ");";
    sqlite3_stmt* stmt = nullptr;
    checkSqlite(sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr), db_, "prepare table info");

    bool found = false;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        if (columnText(stmt, 1) == column) {
            found = true;
            break;
        }
    }

    sqlite3_finalize(stmt);
    return found;
}

void SQLiteDatabase::initialize() {
    execute(
        "CREATE TABLE IF NOT EXISTS accounts ("
        "id TEXT PRIMARY KEY,"
        "provider TEXT NOT NULL,"
        "provider_mode TEXT NOT NULL DEFAULT '',"
        "display_name TEXT NOT NULL,"
        "email TEXT NOT NULL DEFAULT '',"
        "plan_type TEXT NOT NULL DEFAULT '',"
        "runtime_home TEXT NOT NULL DEFAULT '',"
        "credential_ref TEXT NOT NULL DEFAULT '',"
        "status TEXT NOT NULL,"
        "priority INTEGER NOT NULL DEFAULT 100,"
        "enabled INTEGER NOT NULL DEFAULT 1,"
        "consecutive_failures INTEGER NOT NULL DEFAULT 0,"
        "cooldown_until_unix INTEGER NULL,"
        "last_error TEXT NOT NULL DEFAULT '',"
        "created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP"
        ");"
    );

    if (!columnExists("accounts", "runtime_home")) {
        execute("ALTER TABLE accounts ADD COLUMN runtime_home TEXT NOT NULL DEFAULT '';" );
    }
    if (!columnExists("accounts", "email")) {
        execute("ALTER TABLE accounts ADD COLUMN email TEXT NOT NULL DEFAULT '';" );
    }
    if (!columnExists("accounts", "plan_type")) {
        execute("ALTER TABLE accounts ADD COLUMN plan_type TEXT NOT NULL DEFAULT '';" );
    }
    if (!columnExists("accounts", "provider_mode")) {
        execute("ALTER TABLE accounts ADD COLUMN provider_mode TEXT NOT NULL DEFAULT '';" );
    }
    if (!columnExists("accounts", "credential_ref")) {
        execute("ALTER TABLE accounts ADD COLUMN credential_ref TEXT NOT NULL DEFAULT '';" );
    }
    if (!columnExists("accounts", "consecutive_failures")) {
        execute("ALTER TABLE accounts ADD COLUMN consecutive_failures INTEGER NOT NULL DEFAULT 0;" );
    }
    if (!columnExists("accounts", "cooldown_until_unix")) {
        execute("ALTER TABLE accounts ADD COLUMN cooldown_until_unix INTEGER NULL;" );
    }
    if (!columnExists("accounts", "last_error")) {
        execute("ALTER TABLE accounts ADD COLUMN last_error TEXT NOT NULL DEFAULT '';" );
    }

    execute(
        "CREATE TABLE IF NOT EXISTS quota_snapshots ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "account_id TEXT NOT NULL,"
        "captured_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "ordinary_usage_allowed INTEGER NULL,"
        "provider_account_id TEXT NOT NULL DEFAULT ''"
        ");"
    );

    execute(
        "CREATE TABLE IF NOT EXISTS quota_windows ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "snapshot_id INTEGER NOT NULL,"
        "limit_id TEXT NOT NULL DEFAULT '',"
        "limit_name TEXT NOT NULL DEFAULT '',"
        "model TEXT NOT NULL DEFAULT '',"
        "plan_type TEXT NOT NULL DEFAULT '',"
        "reached_type TEXT NOT NULL DEFAULT '',"
        "window_name TEXT NOT NULL DEFAULT '',"
        "used_percent REAL NOT NULL DEFAULT 0,"
        "window_duration_mins INTEGER NULL,"
        "resets_at_unix INTEGER NULL,"
        "FOREIGN KEY(snapshot_id) REFERENCES quota_snapshots(id) ON DELETE CASCADE"
        ");"
    );

    execute(
        "CREATE TABLE IF NOT EXISTS routing_groups ("
        "id TEXT PRIMARY KEY,"
        "display_name TEXT NOT NULL,"
        "strategy TEXT NOT NULL DEFAULT 'HEALTH_FIRST',"
        "enabled INTEGER NOT NULL DEFAULT 1,"
        "manual_account_id TEXT NOT NULL DEFAULT '',"
        "last_index INTEGER NOT NULL DEFAULT -1,"
        "created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP"
        ");"
    );

    execute(
        "CREATE TABLE IF NOT EXISTS routing_group_members ("
        "group_id TEXT NOT NULL,"
        "account_id TEXT NOT NULL,"
        "position INTEGER NOT NULL DEFAULT 0,"
        "PRIMARY KEY(group_id, account_id),"
        "FOREIGN KEY(group_id) REFERENCES routing_groups(id) ON DELETE CASCADE,"
        "FOREIGN KEY(account_id) REFERENCES accounts(id) ON DELETE CASCADE"
        ");"
    );

    execute(
        "CREATE INDEX IF NOT EXISTS idx_quota_snapshots_account_id "
        "ON quota_snapshots(account_id, id DESC);"
    );
    execute(
        "CREATE INDEX IF NOT EXISTS idx_quota_windows_snapshot_id "
        "ON quota_windows(snapshot_id);"
    );
    execute(
        "CREATE INDEX IF NOT EXISTS idx_routing_group_members_group "
        "ON routing_group_members(group_id, position);"
    );
}

void SQLiteDatabase::insertAccount(const Account& account) {
    constexpr const char* sql =
        "INSERT INTO accounts(id, provider, provider_mode, display_name, email, plan_type, runtime_home, "
        "credential_ref, status, priority, enabled, consecutive_failures, cooldown_until_unix, last_error) "
        "VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";

    sqlite3_stmt* stmt = nullptr;
    checkSqlite(sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr), db_, "prepare insert account");

    sqlite3_bind_text(stmt, 1, account.id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, account.provider.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, account.providerMode.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, account.displayName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, account.email.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, account.planType.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, account.runtimeHome.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 8, account.credentialRef.c_str(), -1, SQLITE_TRANSIENT);
    const std::string status = toString(account.status);
    sqlite3_bind_text(stmt, 9, status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 10, account.priority);
    sqlite3_bind_int(stmt, 11, account.enabled ? 1 : 0);
    sqlite3_bind_int(stmt, 12, account.consecutiveFailures);
    bindOptionalInt64(stmt, 13, account.cooldownUntilUnix);
    sqlite3_bind_text(stmt, 14, account.lastError.c_str(), -1, SQLITE_TRANSIENT);

    const int rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        const std::string message = sqlite3_errmsg(db_);
        sqlite3_finalize(stmt);
        throw std::runtime_error("insert account: " + message);
    }

    sqlite3_finalize(stmt);
}

void SQLiteDatabase::updateAccount(const Account& account) {
    constexpr const char* sql =
        "UPDATE accounts SET provider = ?, provider_mode = ?, display_name = ?, email = ?, plan_type = ?, "
        "runtime_home = ?, credential_ref = ?, status = ?, priority = ?, enabled = ?, "
        "consecutive_failures = ?, cooldown_until_unix = ?, last_error = ? WHERE id = ?;";

    sqlite3_stmt* stmt = nullptr;
    checkSqlite(sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr), db_, "prepare update account");

    sqlite3_bind_text(stmt, 1, account.provider.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, account.providerMode.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, account.displayName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, account.email.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, account.planType.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, account.runtimeHome.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, account.credentialRef.c_str(), -1, SQLITE_TRANSIENT);
    const std::string status = toString(account.status);
    sqlite3_bind_text(stmt, 8, status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 9, account.priority);
    sqlite3_bind_int(stmt, 10, account.enabled ? 1 : 0);
    sqlite3_bind_int(stmt, 11, account.consecutiveFailures);
    bindOptionalInt64(stmt, 12, account.cooldownUntilUnix);
    sqlite3_bind_text(stmt, 13, account.lastError.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 14, account.id.c_str(), -1, SQLITE_TRANSIENT);

    const int rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        const std::string message = sqlite3_errmsg(db_);
        sqlite3_finalize(stmt);
        throw std::runtime_error("update account: " + message);
    }

    sqlite3_finalize(stmt);
}

std::optional<Account> SQLiteDatabase::findAccount(const std::string& accountId) const {
    const std::string sql =
        std::string("SELECT ") + accountSelectColumns + " FROM accounts WHERE id = ? LIMIT 1;";

    sqlite3_stmt* stmt = nullptr;
    checkSqlite(sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr), db_, "prepare find account");
    sqlite3_bind_text(stmt, 1, accountId.c_str(), -1, SQLITE_TRANSIENT);

    std::optional<Account> account;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        account = readAccount(stmt);
    }

    sqlite3_finalize(stmt);
    return account;
}

std::vector<Account> SQLiteDatabase::listAccounts() const {
    const std::string sql =
        std::string("SELECT ") + accountSelectColumns + " FROM accounts ORDER BY created_at ASC, id ASC;";

    sqlite3_stmt* stmt = nullptr;
    checkSqlite(sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr), db_, "prepare list accounts");

    std::vector<Account> accounts;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        accounts.push_back(readAccount(stmt));
    }

    sqlite3_finalize(stmt);
    return accounts;
}

std::size_t SQLiteDatabase::countAccounts() const {
    constexpr const char* sql = "SELECT COUNT(*) FROM accounts;";

    sqlite3_stmt* stmt = nullptr;
    checkSqlite(sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr), db_, "prepare count accounts");

    std::size_t count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = static_cast<std::size_t>(sqlite3_column_int64(stmt, 0));
    }

    sqlite3_finalize(stmt);
    return count;
}

void SQLiteDatabase::recordQuotaSnapshot(
    const std::string& accountId,
    const QuotaSnapshot& snapshot) {
    execute("BEGIN IMMEDIATE;");
    try {
        constexpr const char* snapshotSql =
            "INSERT INTO quota_snapshots(account_id, ordinary_usage_allowed, provider_account_id) "
            "VALUES(?, ?, ?);";
        sqlite3_stmt* snapshotStmt = nullptr;
        checkSqlite(
            sqlite3_prepare_v2(db_, snapshotSql, -1, &snapshotStmt, nullptr),
            db_,
            "prepare quota snapshot");

        sqlite3_bind_text(snapshotStmt, 1, accountId.c_str(), -1, SQLITE_TRANSIENT);
        bindOptionalBool(snapshotStmt, 2, snapshot.ordinaryUsageAllowed);
        sqlite3_bind_text(snapshotStmt, 3, snapshot.accountId.c_str(), -1, SQLITE_TRANSIENT);

        const int snapshotRc = sqlite3_step(snapshotStmt);
        if (snapshotRc != SQLITE_DONE) {
            const std::string message = sqlite3_errmsg(db_);
            sqlite3_finalize(snapshotStmt);
            throw std::runtime_error("insert quota snapshot: " + message);
        }
        sqlite3_finalize(snapshotStmt);

        const std::int64_t snapshotId = sqlite3_last_insert_rowid(db_);
        constexpr const char* windowSql =
            "INSERT INTO quota_windows("
            "snapshot_id, limit_id, limit_name, model, plan_type, reached_type, "
            "window_name, used_percent, window_duration_mins, resets_at_unix) "
            "VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";

        sqlite3_stmt* windowStmt = nullptr;
        checkSqlite(
            sqlite3_prepare_v2(db_, windowSql, -1, &windowStmt, nullptr),
            db_,
            "prepare quota window");

        for (const auto& bucket : snapshot.buckets) {
            for (const auto& window : bucket.windows) {
                sqlite3_reset(windowStmt);
                sqlite3_clear_bindings(windowStmt);
                sqlite3_bind_int64(windowStmt, 1, snapshotId);
                sqlite3_bind_text(windowStmt, 2, bucket.limitId.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(windowStmt, 3, bucket.limitName.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(windowStmt, 4, bucket.model.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(windowStmt, 5, bucket.planType.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(windowStmt, 6, bucket.reachedType.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(windowStmt, 7, window.name.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_double(windowStmt, 8, window.usedPercent);
                bindOptionalInt64(windowStmt, 9, window.windowDurationMinutes);
                bindOptionalInt64(windowStmt, 10, window.resetsAtUnix);

                const int windowRc = sqlite3_step(windowStmt);
                if (windowRc != SQLITE_DONE) {
                    const std::string message = sqlite3_errmsg(db_);
                    sqlite3_finalize(windowStmt);
                    throw std::runtime_error("insert quota window: " + message);
                }
            }
        }
        sqlite3_finalize(windowStmt);
        execute("COMMIT;");
    } catch (...) {
        try {
            execute("ROLLBACK;");
        } catch (...) {
        }
        throw;
    }
}

std::vector<QuotaHistoryEntry> SQLiteDatabase::listQuotaHistory(
    const std::string& accountId,
    std::size_t limit) const {
    constexpr const char* sql =
        "SELECT s.id, s.captured_at, s.account_id, s.ordinary_usage_allowed, "
        "s.provider_account_id, w.limit_id, w.limit_name, w.model, w.plan_type, "
        "w.reached_type, w.window_name, w.used_percent, w.window_duration_mins, "
        "w.resets_at_unix "
        "FROM quota_snapshots s "
        "JOIN quota_windows w ON w.snapshot_id = s.id "
        "WHERE s.account_id = ? "
        "ORDER BY s.id DESC, w.id ASC "
        "LIMIT ?;";

    sqlite3_stmt* stmt = nullptr;
    checkSqlite(
        sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr),
        db_,
        "prepare quota history");

    sqlite3_bind_text(stmt, 1, accountId.c_str(), -1, SQLITE_TRANSIENT);
    const auto boundedLimit = std::clamp<std::size_t>(limit, 1, 500);
    sqlite3_bind_int64(stmt, 2, static_cast<sqlite3_int64>(boundedLimit));

    std::vector<QuotaHistoryEntry> entries;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        QuotaHistoryEntry entry;
        entry.snapshotId = sqlite3_column_int64(stmt, 0);
        entry.capturedAt = columnText(stmt, 1);
        entry.accountId = columnText(stmt, 2);
        entry.ordinaryUsageAllowed = columnOptionalBool(stmt, 3);
        entry.providerAccountId = columnText(stmt, 4);
        entry.limitId = columnText(stmt, 5);
        entry.limitName = columnText(stmt, 6);
        entry.model = columnText(stmt, 7);
        entry.planType = columnText(stmt, 8);
        entry.reachedType = columnText(stmt, 9);
        entry.windowName = columnText(stmt, 10);
        entry.usedPercent = sqlite3_column_double(stmt, 11);
        entry.windowDurationMinutes = columnOptionalInt64(stmt, 12);
        entry.resetsAtUnix = columnOptionalInt64(stmt, 13);
        entries.push_back(std::move(entry));
    }

    sqlite3_finalize(stmt);
    return entries;
}

void SQLiteDatabase::saveRoutingGroup(const RoutingGroup& group) {
    execute("BEGIN IMMEDIATE;");
    try {
        constexpr const char* groupSql =
            "INSERT INTO routing_groups(id, display_name, strategy, enabled, manual_account_id, last_index) "
            "VALUES(?, ?, ?, ?, ?, ?) "
            "ON CONFLICT(id) DO UPDATE SET display_name=excluded.display_name, strategy=excluded.strategy, "
            "enabled=excluded.enabled, manual_account_id=excluded.manual_account_id, last_index=excluded.last_index;";

        sqlite3_stmt* groupStmt = nullptr;
        checkSqlite(sqlite3_prepare_v2(db_, groupSql, -1, &groupStmt, nullptr), db_, "prepare routing group");
        sqlite3_bind_text(groupStmt, 1, group.id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(groupStmt, 2, group.displayName.c_str(), -1, SQLITE_TRANSIENT);
        const std::string strategy = toString(group.strategy);
        sqlite3_bind_text(groupStmt, 3, strategy.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(groupStmt, 4, group.enabled ? 1 : 0);
        sqlite3_bind_text(groupStmt, 5, group.manualAccountId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(groupStmt, 6, group.lastIndex);
        checkSqlite(sqlite3_step(groupStmt), db_, "save routing group");
        sqlite3_finalize(groupStmt);

        sqlite3_stmt* deleteStmt = nullptr;
        checkSqlite(
            sqlite3_prepare_v2(db_, "DELETE FROM routing_group_members WHERE group_id = ?;", -1, &deleteStmt, nullptr),
            db_,
            "prepare routing member delete");
        sqlite3_bind_text(deleteStmt, 1, group.id.c_str(), -1, SQLITE_TRANSIENT);
        checkSqlite(sqlite3_step(deleteStmt), db_, "delete routing members");
        sqlite3_finalize(deleteStmt);

        constexpr const char* memberSql =
            "INSERT INTO routing_group_members(group_id, account_id, position) VALUES(?, ?, ?);";
        sqlite3_stmt* memberStmt = nullptr;
        checkSqlite(sqlite3_prepare_v2(db_, memberSql, -1, &memberStmt, nullptr), db_, "prepare routing member");
        for (std::size_t i = 0; i < group.accountIds.size(); ++i) {
            sqlite3_reset(memberStmt);
            sqlite3_clear_bindings(memberStmt);
            sqlite3_bind_text(memberStmt, 1, group.id.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(memberStmt, 2, group.accountIds[i].c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(memberStmt, 3, static_cast<sqlite3_int64>(i));
            checkSqlite(sqlite3_step(memberStmt), db_, "save routing member");
        }
        sqlite3_finalize(memberStmt);
        execute("COMMIT;");
    } catch (...) {
        try {
            execute("ROLLBACK;");
        } catch (...) {
        }
        throw;
    }
}

std::optional<RoutingGroup> SQLiteDatabase::findRoutingGroup(const std::string& groupId) const {
    constexpr const char* groupSql =
        "SELECT id, display_name, strategy, enabled, manual_account_id, last_index "
        "FROM routing_groups WHERE id = ? LIMIT 1;";
    sqlite3_stmt* groupStmt = nullptr;
    checkSqlite(sqlite3_prepare_v2(db_, groupSql, -1, &groupStmt, nullptr), db_, "prepare find routing group");
    sqlite3_bind_text(groupStmt, 1, groupId.c_str(), -1, SQLITE_TRANSIENT);

    RoutingGroup group;
    if (sqlite3_step(groupStmt) != SQLITE_ROW) {
        sqlite3_finalize(groupStmt);
        return std::nullopt;
    }
    group.id = columnText(groupStmt, 0);
    group.displayName = columnText(groupStmt, 1);
    group.strategy = routingStrategyFromString(columnText(groupStmt, 2));
    group.enabled = sqlite3_column_int(groupStmt, 3) != 0;
    group.manualAccountId = columnText(groupStmt, 4);
    group.lastIndex = sqlite3_column_int(groupStmt, 5);
    sqlite3_finalize(groupStmt);

    constexpr const char* memberSql =
        "SELECT account_id FROM routing_group_members WHERE group_id = ? ORDER BY position ASC;";
    sqlite3_stmt* memberStmt = nullptr;
    checkSqlite(sqlite3_prepare_v2(db_, memberSql, -1, &memberStmt, nullptr), db_, "prepare routing members");
    sqlite3_bind_text(memberStmt, 1, groupId.c_str(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(memberStmt) == SQLITE_ROW) {
        group.accountIds.push_back(columnText(memberStmt, 0));
    }
    sqlite3_finalize(memberStmt);
    return group;
}

std::vector<RoutingGroup> SQLiteDatabase::listRoutingGroups() const {
    constexpr const char* sql = "SELECT id FROM routing_groups ORDER BY created_at ASC, id ASC;";
    sqlite3_stmt* stmt = nullptr;
    checkSqlite(sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr), db_, "prepare list routing groups");

    std::vector<std::string> ids;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ids.push_back(columnText(stmt, 0));
    }
    sqlite3_finalize(stmt);

    std::vector<RoutingGroup> groups;
    groups.reserve(ids.size());
    for (const auto& id : ids) {
        if (auto group = findRoutingGroup(id)) {
            groups.push_back(std::move(*group));
        }
    }
    return groups;
}

void SQLiteDatabase::updateRoutingGroupCursor(const std::string& groupId, int lastIndex) {
    constexpr const char* sql = "UPDATE routing_groups SET last_index = ? WHERE id = ?;";
    sqlite3_stmt* stmt = nullptr;
    checkSqlite(sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr), db_, "prepare routing cursor update");
    sqlite3_bind_int(stmt, 1, lastIndex);
    sqlite3_bind_text(stmt, 2, groupId.c_str(), -1, SQLITE_TRANSIENT);
    checkSqlite(sqlite3_step(stmt), db_, "update routing cursor");
    sqlite3_finalize(stmt);
}

}  // namespace routerai
