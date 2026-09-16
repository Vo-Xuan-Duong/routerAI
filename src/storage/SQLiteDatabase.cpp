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
    account.displayName = columnText(stmt, 2);
    account.email = columnText(stmt, 3);
    account.planType = columnText(stmt, 4);
    account.runtimeHome = columnText(stmt, 5);
    account.status = statusFromString(columnText(stmt, 6));
    account.priority = sqlite3_column_int(stmt, 7);
    account.enabled = sqlite3_column_int(stmt, 8) != 0;
    return account;
}

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
        "display_name TEXT NOT NULL,"
        "email TEXT NOT NULL DEFAULT '',"
        "plan_type TEXT NOT NULL DEFAULT '',"
        "runtime_home TEXT NOT NULL DEFAULT '',"
        "status TEXT NOT NULL,"
        "priority INTEGER NOT NULL DEFAULT 100,"
        "enabled INTEGER NOT NULL DEFAULT 1,"
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
        "CREATE INDEX IF NOT EXISTS idx_quota_snapshots_account_id "
        "ON quota_snapshots(account_id, id DESC);"
    );
    execute(
        "CREATE INDEX IF NOT EXISTS idx_quota_windows_snapshot_id "
        "ON quota_windows(snapshot_id);"
    );
}

void SQLiteDatabase::insertAccount(const Account& account) {
    constexpr const char* sql =
        "INSERT INTO accounts(id, provider, display_name, email, plan_type, runtime_home, status, priority, enabled) "
        "VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?);";

    sqlite3_stmt* stmt = nullptr;
    checkSqlite(sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr), db_, "prepare insert account");

    sqlite3_bind_text(stmt, 1, account.id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, account.provider.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, account.displayName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, account.email.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, account.planType.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, account.runtimeHome.c_str(), -1, SQLITE_TRANSIENT);
    const std::string status = toString(account.status);
    sqlite3_bind_text(stmt, 7, status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 8, account.priority);
    sqlite3_bind_int(stmt, 9, account.enabled ? 1 : 0);

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
        "UPDATE accounts SET provider = ?, display_name = ?, email = ?, plan_type = ?, runtime_home = ?, "
        "status = ?, priority = ?, enabled = ? WHERE id = ?;";

    sqlite3_stmt* stmt = nullptr;
    checkSqlite(sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr), db_, "prepare update account");

    sqlite3_bind_text(stmt, 1, account.provider.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, account.displayName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, account.email.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, account.planType.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, account.runtimeHome.c_str(), -1, SQLITE_TRANSIENT);
    const std::string status = toString(account.status);
    sqlite3_bind_text(stmt, 6, status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 7, account.priority);
    sqlite3_bind_int(stmt, 8, account.enabled ? 1 : 0);
    sqlite3_bind_text(stmt, 9, account.id.c_str(), -1, SQLITE_TRANSIENT);

    const int rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        const std::string message = sqlite3_errmsg(db_);
        sqlite3_finalize(stmt);
        throw std::runtime_error("update account: " + message);
    }

    sqlite3_finalize(stmt);
}

std::optional<Account> SQLiteDatabase::findAccount(const std::string& accountId) const {
    constexpr const char* sql =
        "SELECT id, provider, display_name, email, plan_type, runtime_home, status, priority, enabled "
        "FROM accounts WHERE id = ? LIMIT 1;";

    sqlite3_stmt* stmt = nullptr;
    checkSqlite(sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr), db_, "prepare find account");
    sqlite3_bind_text(stmt, 1, accountId.c_str(), -1, SQLITE_TRANSIENT);

    std::optional<Account> account;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        account = readAccount(stmt);
    }

    sqlite3_finalize(stmt);
    return account;
}

std::vector<Account> SQLiteDatabase::listAccounts() const {
    constexpr const char* sql =
        "SELECT id, provider, display_name, email, plan_type, runtime_home, status, priority, enabled "
        "FROM accounts ORDER BY created_at ASC, id ASC;";

    sqlite3_stmt* stmt = nullptr;
    checkSqlite(sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr), db_, "prepare list accounts");

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

}  // namespace routerai
