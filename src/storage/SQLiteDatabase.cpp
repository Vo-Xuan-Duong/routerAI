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

std::string columnText(sqlite3_stmt* stmt, int index) {
    const auto* value = sqlite3_column_text(stmt, index);
    return value ? reinterpret_cast<const char*>(value) : std::string{};
}

Account readAccount(sqlite3_stmt* stmt) {
    Account account;
    account.id = columnText(stmt, 0);
    account.provider = columnText(stmt, 1);
    account.displayName = columnText(stmt, 2);
    account.runtimeHome = columnText(stmt, 3);
    account.status = statusFromString(columnText(stmt, 4));
    account.priority = sqlite3_column_int(stmt, 5);
    account.enabled = sqlite3_column_int(stmt, 6) != 0;
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
}

void SQLiteDatabase::insertAccount(const Account& account) {
    constexpr const char* sql =
        "INSERT INTO accounts(id, provider, display_name, runtime_home, status, priority, enabled) "
        "VALUES(?, ?, ?, ?, ?, ?, ?);";

    sqlite3_stmt* stmt = nullptr;
    checkSqlite(sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr), db_, "prepare insert account");

    sqlite3_bind_text(stmt, 1, account.id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, account.provider.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, account.displayName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, account.runtimeHome.c_str(), -1, SQLITE_TRANSIENT);
    const std::string status = toString(account.status);
    sqlite3_bind_text(stmt, 5, status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 6, account.priority);
    sqlite3_bind_int(stmt, 7, account.enabled ? 1 : 0);

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
        "UPDATE accounts SET provider = ?, display_name = ?, runtime_home = ?, "
        "status = ?, priority = ?, enabled = ? WHERE id = ?;";

    sqlite3_stmt* stmt = nullptr;
    checkSqlite(sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr), db_, "prepare update account");

    sqlite3_bind_text(stmt, 1, account.provider.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, account.displayName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, account.runtimeHome.c_str(), -1, SQLITE_TRANSIENT);
    const std::string status = toString(account.status);
    sqlite3_bind_text(stmt, 4, status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 5, account.priority);
    sqlite3_bind_int(stmt, 6, account.enabled ? 1 : 0);
    sqlite3_bind_text(stmt, 7, account.id.c_str(), -1, SQLITE_TRANSIENT);

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
        "SELECT id, provider, display_name, runtime_home, status, priority, enabled "
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
        "SELECT id, provider, display_name, runtime_home, status, priority, enabled "
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

}  // namespace routerai
