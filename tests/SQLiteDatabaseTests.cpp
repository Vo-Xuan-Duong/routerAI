#include "core/Account.hpp"
#include "core/Quota.hpp"
#include "storage/SQLiteDatabase.hpp"

#include <sqlite3.h>

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void createLegacyDatabase(const std::filesystem::path& path) {
    sqlite3* db = nullptr;
    if (sqlite3_open(path.string().c_str(), &db) != SQLITE_OK) {
        const std::string message = db ? sqlite3_errmsg(db) : "unknown SQLite error";
        if (db) sqlite3_close(db);
        throw std::runtime_error("legacy sqlite open failed: " + message);
    }

    const char* sql =
        "CREATE TABLE accounts ("
        "id TEXT PRIMARY KEY,"
        "provider TEXT NOT NULL,"
        "display_name TEXT NOT NULL,"
        "status TEXT NOT NULL,"
        "priority INTEGER NOT NULL DEFAULT 100,"
        "enabled INTEGER NOT NULL DEFAULT 1,"
        "created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP"
        ");"
        "INSERT INTO accounts(id, provider, display_name, status, priority, enabled) "
        "VALUES('legacy-01', 'codex', 'Legacy account', 'READY', 75, 1);";

    char* error = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &error) != SQLITE_OK) {
        const std::string message = error ? error : sqlite3_errmsg(db);
        sqlite3_free(error);
        sqlite3_close(db);
        throw std::runtime_error("legacy schema setup failed: " + message);
    }

    sqlite3_close(db);
}

}  // namespace

int main() {
    const auto dbPath = std::filesystem::current_path() / "routerai-storage-test.db";
    std::filesystem::remove(dbPath);

    try {
        createLegacyDatabase(dbPath);

        routerai::SQLiteDatabase database(dbPath.string());
        database.initialize();

        const auto legacy = database.findAccount("legacy-01");
        require(legacy.has_value(), "legacy account was not preserved by migration");
        require(legacy->runtimeHome.empty(), "legacy runtime_home default is not empty");
        require(legacy->email.empty(), "legacy email default is not empty");
        require(legacy->planType.empty(), "legacy plan_type default is not empty");
        require(legacy->priority == 75, "legacy account priority changed during migration");

        routerai::Account account;
        account.id = "codex-01";
        account.provider = "codex";
        account.displayName = "user@example.com";
        account.email = "user@example.com";
        account.planType = "plus";
        account.runtimeHome = ".routerai/accounts/codex-01/codex-home";
        account.status = routerai::AccountStatus::Ready;
        account.priority = 100;
        database.insertAccount(account);

        const auto stored = database.findAccount(account.id);
        require(stored.has_value(), "new account was not persisted");
        require(stored->email == account.email, "email was not persisted");
        require(stored->planType == account.planType, "plan type was not persisted");
        require(stored->runtimeHome == account.runtimeHome, "runtime home was not persisted");

        routerai::QuotaSnapshot snapshot;
        snapshot.ordinaryUsageAllowed = true;
        snapshot.accountId = "provider-account-123";

        routerai::QuotaBucket bucket;
        bucket.limitId = "codex";
        bucket.limitName = "Codex";
        bucket.model = "gpt-5.6-codex";
        bucket.planType = "plus";

        routerai::QuotaWindow primary;
        primary.name = "primary";
        primary.usedPercent = 42.5;
        primary.windowDurationMinutes = 300;
        primary.resetsAtUnix = 1800000000;
        bucket.windows.push_back(primary);

        routerai::QuotaWindow secondary;
        secondary.name = "secondary";
        secondary.usedPercent = 11.0;
        secondary.windowDurationMinutes = 10080;
        secondary.resetsAtUnix = 1800500000;
        bucket.windows.push_back(secondary);

        snapshot.buckets.push_back(bucket);
        database.recordQuotaSnapshot(account.id, snapshot);

        const auto history = database.listQuotaHistory(account.id, 10);
        require(history.size() == 2, "expected two quota history window rows");
        require(history[0].snapshotId == history[1].snapshotId, "history rows were not grouped in one snapshot");
        require(history[0].ordinaryUsageAllowed == true, "ordinary usage permission was not persisted");
        require(history[0].providerAccountId == snapshot.accountId, "provider account id was not persisted");
        require(history[0].limitId == "codex", "quota bucket id was not persisted");
        require(history[0].windowName == "primary", "primary window ordering changed");
        require(history[0].usedPercent == 42.5, "primary used percentage changed");
        require(history[1].windowName == "secondary", "secondary window ordering changed");
        require(history[1].windowDurationMinutes == 10080, "secondary duration changed");

        std::cout << "SQLiteDatabaseTests: OK\n";
    } catch (const std::exception& exception) {
        std::cerr << "SQLiteDatabaseTests: FAILED: " << exception.what() << '\n';
        std::filesystem::remove(dbPath);
        return 1;
    }

    std::filesystem::remove(dbPath);
    return 0;
}
