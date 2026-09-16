cmake_minimum_required(VERSION 3.24)

project(routerAI VERSION 0.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(CLI11 CONFIG REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_executable(router
    src/main.cpp
    src/core/AccountManager.cpp
    src/providers/codex/CodexAppServerClient.cpp
    src/providers/codex/CodexCli.cpp
    src/providers/codex/CodexProvider.cpp
    src/storage/SQLiteDatabase.cpp
    src/system/DuplexProcess.cpp
    src/system/ProcessRunner.cpp
)

target_include_directories(router PRIVATE src)

target_link_libraries(router PRIVATE
    CLI11::CLI11
    unofficial::sqlite3::sqlite3
    spdlog::spdlog
    nlohmann_json::nlohmann_json
)

if(MSVC)
    target_compile_options(router PRIVATE /W4 /permissive-)
else()
    target_compile_options(router PRIVATE -Wall -Wextra -Wpedantic)
endif()
