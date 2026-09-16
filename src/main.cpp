#include "core/AccountManager.hpp"
#include "storage/SQLiteDatabase.hpp"

#include <CLI/CLI.hpp>
#include <spdlog/spdlog.h>

#include <iomanip>
#include <iostream>
#include <string>

namespace {

void printAccounts(const std::vector<routerai::Account>& accounts) {
    if (accounts.empty()) {
        std::cout << "No accounts configured.\n";
        return;
    }

    std::cout << std::left
              << std::setw(14) << "ID"
              << std::setw(12) << "PROVIDER"
              << std::setw(18) << "STATUS"
              << std::setw(10) << "PRIORITY"
              << "NAME\n";

    std::cout << std::string(72, '-') << '\n';

    for (const auto& account : accounts) {
        std::cout << std::left
                  << std::setw(14) << account.id
                  << std::setw(12) << account.provider
                  << std::setw(18) << routerai::toString(account.status)
                  << std::setw(10) << account.priority
                  << account.displayName << '\n';
    }
}

}  // namespace

int main(int argc, char** argv) {
    try {
        routerai::SQLiteDatabase database("router.db");
        database.initialize();
        routerai::AccountManager accounts(database);

        CLI::App app{"routerAI - console-first AI account router"};
        app.set_version_flag("--version", "routerAI 0.1.0");
        app.require_subcommand(1);

        auto* status = app.add_subcommand("status", "Show local router status");
        status->callback([&]() {
            std::cout << "Router:   OK\n";
            std::cout << "Database: OK (" << database.path() << ")\n";
            std::cout << "Accounts: " << accounts.countAccounts() << '\n';
        });

        auto* account = app.add_subcommand("account", "Manage provider accounts");
        account->require_subcommand(1);

        auto* list = account->add_subcommand("list", "List configured accounts");
        list->callback([&]() {
            printAccounts(accounts.listAccounts());
        });

        auto* add = account->add_subcommand("add", "Add a provider account");
        add->require_subcommand(1);

        auto* addCodex = add->add_subcommand("codex", "Add a Codex account placeholder");
        addCodex->callback([&]() {
            const auto created = accounts.addCodexPlaceholder();
            std::cout << "Codex account placeholder created.\n\n";
            std::cout << "ID       : " << created.id << '\n';
            std::cout << "Provider : " << created.provider << '\n';
            std::cout << "Status   : " << routerai::toString(created.status) << '\n';
            std::cout << "Next     : implement OAuth authentication\n";
        });

        CLI11_PARSE(app, argc, argv);
        return 0;
    } catch (const std::exception& exception) {
        spdlog::error("{}", exception.what());
        return 1;
    }
}
