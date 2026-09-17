#include "storage/SQLiteDatabase.hpp"

#include <sqlite3.h>

#include <algorithm>
#include <stdexcept>
#include <string>

namespace routerai {
namespace {

void check(int rc, sqlite3* db, const char* context) {
    if (rc != SQLITE_OK && rc != SQLITE_DONE && rc != SQLITE_ROW) {
        throw std::runtime_error(std::string(context) + ": " + sqlite3_errmsg(db));
    }
}

std::string text(sqlite3_stmt* stmt, int index) {
    const auto* value = sqlite3_column_text(stmt, index);
    return value ? reinterpret_cast<const char*>(value) : std::string{};
}

void ensureRequestLogTable(sqlite3* db) {
    constexpr const char* sql =
        "CREATE TABLE IF NOT EXISTS request_logs ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "group_id TEXT NOT NULL DEFAULT '',"
        "account_id TEXT NOT NULL DEFAULT '',"
        "provider TEXT NOT NULL DEFAULT '',"
        "model TEXT NOT NULL DEFAULT '',"
        "status_code INTEGER NOT NULL DEFAULT 0,"
        "duration_ms INTEGER NOT NULL DEFAULT 0,"
        "streaming INTEGER NOT NULL DEFAULT 0,"
        "success INTEGER NOT NULL DEFAULT 0,"
        "error TEXT NOT NULL DEFAULT ''"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_request_logs_created_at ON request_logs(id DESC);";
    char* error = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &error) != SQLITE_OK) {
        const std::string message = error ? error : sqlite3_errmsg(db);
        sqlite3_free(error);
        throw std::runtime_error("initialize request logs: " + message);
    }
}

void exec(sqlite3* db, const char* sql) {
    char* error = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &error) != SQLITE_OK) {
        const std::string message = error ? error : sqlite3_errmsg(db);
        sqlite3_free(error);
        throw std::runtime_error(message);
    }
}

void deleteWhere(sqlite3* db, const char* sql, const std::string& id) {
    sqlite3_stmt* stmt = nullptr;
    check(sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr), db, "prepare delete");
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    const int rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        const std::string message = sqlite3_errmsg(db);
        sqlite3_finalize(stmt);
        throw std::runtime_error(message);
    }
    sqlite3_finalize(stmt);
}

}  // namespace

void SQLiteDatabase::deleteAccount(const std::string& accountId) {
    exec(db_, "BEGIN IMMEDIATE;");
    try {
        deleteWhere(db_, "DELETE FROM routing_group_members WHERE account_id = ?;", accountId);
        deleteWhere(
            db_,
            "DELETE FROM quota_windows WHERE snapshot_id IN (SELECT id FROM quota_snapshots WHERE account_id = ?);",
            accountId);
        deleteWhere(db_, "DELETE FROM quota_snapshots WHERE account_id = ?;", accountId);
        deleteWhere(db_, "DELETE FROM accounts WHERE id = ?;", accountId);
        exec(db_, "COMMIT;");
    } catch (...) {
        try { exec(db_, "ROLLBACK;"); } catch (...) {}
        throw;
    }
}

void SQLiteDatabase::recordRequestLog(const RequestLogEntry& entry) {
    ensureRequestLogTable(db_);
    constexpr const char* sql =
        "INSERT INTO request_logs(group_id, account_id, provider, model, status_code, duration_ms, streaming, success, error) "
        "VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    check(sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr), db_, "prepare request log");
    sqlite3_bind_text(stmt, 1, entry.groupId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, entry.accountId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, entry.provider.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, entry.model.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 5, static_cast<sqlite3_int64>(entry.statusCode));
    sqlite3_bind_int64(stmt, 6, entry.durationMs);
    sqlite3_bind_int(stmt, 7, entry.streaming ? 1 : 0);
    sqlite3_bind_int(stmt, 8, entry.success ? 1 : 0);
    sqlite3_bind_text(stmt, 9, entry.error.c_str(), -1, SQLITE_TRANSIENT);
    check(sqlite3_step(stmt), db_, "insert request log");
    sqlite3_finalize(stmt);
}

std::vector<RequestLogEntry> SQLiteDatabase::listRequestLogs(std::size_t limit) const {
    ensureRequestLogTable(db_);
    constexpr const char* sql =
        "SELECT id, created_at, group_id, account_id, provider, model, status_code, duration_ms, streaming, success, error "
        "FROM request_logs ORDER BY id DESC LIMIT ?;";
    sqlite3_stmt* stmt = nullptr;
    check(sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr), db_, "prepare request log list");
    const auto bounded = std::clamp<std::size_t>(limit, 1, 2000);
    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(bounded));

    std::vector<RequestLogEntry> result;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        RequestLogEntry entry;
        entry.id = sqlite3_column_int64(stmt, 0);
        entry.createdAt = text(stmt, 1);
        entry.groupId = text(stmt, 2);
        entry.accountId = text(stmt, 3);
        entry.provider = text(stmt, 4);
        entry.model = text(stmt, 5);
        entry.statusCode = static_cast<long>(sqlite3_column_int64(stmt, 6));
        entry.durationMs = sqlite3_column_int64(stmt, 7);
        entry.streaming = sqlite3_column_int(stmt, 8) != 0;
        entry.success = sqlite3_column_int(stmt, 9) != 0;
        entry.error = text(stmt, 10);
        result.push_back(std::move(entry));
    }
    sqlite3_finalize(stmt);
    return result;
}

void SQLiteDatabase::clearRequestLogs() {
    ensureRequestLogTable(db_);
    exec(db_, "DELETE FROM request_logs;");
}

}  // namespace routerai
