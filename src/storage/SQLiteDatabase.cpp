#include "storage/SQLiteDatabase.hpp"

#include <sqlite3.h>

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

void SQLiteDatabase::initialize() {
    execute(
        "CREATE TABLE IF NOT EXISTS accounts ("
        "id TEXT PRIMARY KEY,"
        "provider TEXT NOT NULL,"
        "display_name TEXT NOT NULL,"
        "status TEXT NOT NULL,"
        "priority INTEGER NOT NULL DEFAULT 100,"
        "enabled INTEGER NOT NULL DEFAULT 1,"
        "created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP"
        ");"
    );
}

void SQLiteDatabase::insertAccount(const Account& account) {
    constexpr const char* sql =
        "INSERT INTO accounts(id, provider, display_name, status, priority, enabled) "
        "VALUES(?, ?, ?, ?, ?, ?);";

    sqlite3_stmt* stmt = nullptr;
    checkSqlite(sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr), db_, "prepare insert account");

    sqlite3_bind_text(stmt, 1, account.id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, account.provider.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, account.displayName.c_str(), -1, SQLITE_TRANSIENT);
    const std::string status = toString(account.status);
    sqlite3_bind_text(stmt, 4, status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 5, account.priority);
    sqlite3_bind_int(stmt, 6, account.enabled ? 1 : 0);

    const int rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        const std::string message = sqlite3_errmsg(db_);
        sqlite3_finalize(stmt);
        throw std::runtime_error("insert account: " + message);
    }

    sqlite3_finalize(stmt);
}

std::vector<Account> SQLiteDatabase::listAccounts() const {
    constexpr const char* sql =
        "SELECT id, provider, display_name, status, priority, enabled "
        "FROM accounts ORDER BY created_at ASC, id ASC;";

    sqlite3_stmt* stmt = nullptr;
    checkSqlite(sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr), db_, "prepare list accounts");

    std::vector<Account> accounts;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Account account;
        account.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        account.provider = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        account.displayName = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        account.status = statusFromString(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3)));
        account.priority = sqlite3_column_int(stmt, 4);
        account.enabled = sqlite3_column_int(stmt, 5) != 0;
        accounts.push_back(std::move(account));
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

}  // namespace routerai
