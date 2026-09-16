#pragma once

#include "core/Account.hpp"
#include "storage/SQLiteDatabase.hpp"

#include <string>
#include <vector>

namespace routerai {

class AccountManager {
public:
    explicit AccountManager(SQLiteDatabase& database);

    Account addCodexPlaceholder();
    std::vector<Account> listAccounts() const;
    std::size_t countAccounts() const;

private:
    SQLiteDatabase& database_;

    std::string nextAccountId(const std::string& provider) const;
};

}  // namespace routerai
