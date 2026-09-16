cmake_minimum_required(VERSION 3.24)

project(routerAI VERSION 0.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(unofficial-sqlite3 CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_executable(router
    src/main.cpp
    src/core/AccountManager.cpp
    src/core/AccountSelector.cpp
    src/providers/codex/CodexAppServerClient.cpp
    src/providers/codex/CodexCli.cpp
    src/providers/codex/CodexProvider.cpp
    src/storage/SQLiteDatabase.cpp
    src/system/DuplexProcess.cpp
    src/system/ProcessRunner.cpp
    src/ui/TerminalApp.cpp
)

target_include_directories(router PRIVATE src)

target_link_libraries(router PRIVATE
    unofficial::sqlite3::sqlite3
    spdlog::spdlog
    nlohmann_json::nlohmann_json
)

if(MSVC)
    target_compile_options(router PRIVATE /W4 /permissive-)
else()
    target_compile_options(router PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(router_storage_tests
        tests/SQLiteDatabaseTests.cpp
        src/storage/SQLiteDatabase.cpp
    )
    target_include_directories(router_storage_tests PRIVATE src)
    target_link_libraries(router_storage_tests PRIVATE
        unofficial::sqlite3::sqlite3
    )

    add_executable(router_selector_tests
        tests/AccountSelectorTests.cpp
        src/core/AccountSelector.cpp
    )
    target_include_directories(router_selector_tests PRIVATE src)

    if(MSVC)
        target_compile_options(router_storage_tests PRIVATE /W4 /permissive-)
        target_compile_options(router_selector_tests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(router_storage_tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(router_selector_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_test(NAME sqlite_storage COMMAND router_storage_tests)
    add_test(NAME account_selector COMMAND router_selector_tests)
endif()
