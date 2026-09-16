#pragma once

#include "core/Account.hpp"

#include <cstddef>
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
    std::vector<Account> listAccounts() const;
    std::size_t countAccounts() const;

    const std::string& path() const noexcept { return path_; }

private:
    std::string path_;
    sqlite3* db_{nullptr};

    void execute(const char* sql) const;
};

}  // namespace routerai
