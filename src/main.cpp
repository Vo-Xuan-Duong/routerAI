#include "core/AccountManager.hpp"
#include "storage/SQLiteDatabase.hpp"
#include "ui/TerminalApp.hpp"

#include <spdlog/spdlog.h>

#include <exception>

int main() {
    try {
        routerai::SQLiteDatabase database("router.db");
        database.initialize();

        routerai::AccountManager accounts(database);
        routerai::TerminalApp app(database, accounts);
        return app.run();
    } catch (const std::exception& exception) {
        spdlog::error("{}", exception.what());
        return 1;
    }
}
