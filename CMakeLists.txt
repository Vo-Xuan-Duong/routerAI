cmake_minimum_required(VERSION 3.24)

project(routerAI VERSION 0.8.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Version.hpp.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/Version.hpp
    @ONLY
)

find_package(CURL REQUIRED)
find_package(httplib CONFIG REQUIRED)
find_package(ftxui CONFIG REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_executable(router
    src/main.cpp
    src/api/LocalApiServer.cpp
    src/api/LocalApiProviderAdmin.cpp
    src/api/OpenAICompat.cpp
    src/core/AccountManager.cpp
    src/core/AccountManagerLifecycle.cpp
    src/core/AccountSelector.cpp
    src/core/CompletionRouter.cpp
    src/core/CompletionStream.cpp
    src/core/ConfigManager.cpp
    src/core/FailurePolicy.cpp
    src/core/MaintenanceManager.cpp
    src/core/RoutingManager.cpp
    src/desktop/DesktopAccountSwitcher.cpp
    src/desktop/DesktopProfileManager.cpp
    src/net/HttpClient.cpp
    src/providers/antigravity/AntigravityApiClient.cpp
    src/providers/antigravity/AntigravityCli.cpp
    src/providers/antigravity/AntigravityProvider.cpp
    src/providers/codex/CodexAppServerClient.cpp
    src/providers/codex/CodexCli.cpp
    src/providers/codex/CodexProvider.cpp
    src/providers/zai/ZaiClient.cpp
    src/providers/zai/ZaiProvider.cpp
    src/security/CredentialStore.cpp
    src/storage/SQLiteDatabase.cpp
    src/storage/SQLiteDatabaseExtras.cpp
    src/system/DuplexProcess.cpp
    src/system/ProcessRunner.cpp
    src/ui/TerminalApp.cpp
    src/ui/TerminalAppDesktop.cpp
    src/ui/TerminalAppManagement.cpp
)

target_include_directories(router PRIVATE src ${CMAKE_CURRENT_BINARY_DIR}/generated)

target_link_libraries(router PRIVATE
    CURL::libcurl httplib::httplib
    ftxui::screen ftxui::dom ftxui::component
    unofficial::sqlite3::sqlite3 spdlog::spdlog nlohmann_json::nlohmann_json
)

if(WIN32)
    target_link_libraries(router PRIVATE crypt32)
endif()

if(MSVC)
    target_compile_options(router PRIVATE /W4 /permissive-)
else()
    target_compile_options(router PRIVATE -Wall -Wextra -Wpedantic)
endif()

install(TARGETS router RUNTIME DESTINATION .)
set(CPACK_PACKAGE_NAME "routerAI")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_FILE_NAME "routerAI-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
if(WIN32)
    set(CPACK_GENERATOR "ZIP")
else()
    set(CPACK_GENERATOR "TGZ")
endif()
include(CPack)

include(CTest)
if(BUILD_TESTING)
    add_executable(router_storage_tests
        tests/SQLiteDatabaseTests.cpp
        src/storage/SQLiteDatabase.cpp
        src/storage/SQLiteDatabaseExtras.cpp
    )
    target_include_directories(router_storage_tests PRIVATE src)
    target_link_libraries(router_storage_tests PRIVATE unofficial::sqlite3::sqlite3)

    add_executable(router_selector_tests
        tests/AccountSelectorTests.cpp
        src/core/AccountSelector.cpp
        src/core/FailurePolicy.cpp
    )
    target_include_directories(router_selector_tests PRIVATE src)

    add_executable(router_routing_tests
        tests/RoutingManagerTests.cpp
        src/core/AccountSelector.cpp
        src/core/FailurePolicy.cpp
        src/core/RoutingManager.cpp
        src/storage/SQLiteDatabase.cpp
        src/storage/SQLiteDatabaseExtras.cpp
    )
    target_include_directories(router_routing_tests PRIVATE src)
    target_link_libraries(router_routing_tests PRIVATE unofficial::sqlite3::sqlite3)

    add_executable(router_openai_compat_tests tests/OpenAICompatTests.cpp src/api/OpenAICompat.cpp)
    target_include_directories(router_openai_compat_tests PRIVATE src)
    target_link_libraries(router_openai_compat_tests PRIVATE nlohmann_json::nlohmann_json)

    add_executable(router_provider_metadata_tests
        tests/ProviderMetadataTests.cpp
        src/net/HttpClient.cpp
        src/providers/antigravity/AntigravityApiClient.cpp
        src/providers/zai/ZaiClient.cpp
        src/providers/zai/ZaiProvider.cpp
    )
    target_include_directories(router_provider_metadata_tests PRIVATE src ${CMAKE_CURRENT_BINARY_DIR}/generated)
    target_link_libraries(router_provider_metadata_tests PRIVATE CURL::libcurl)

    add_executable(router_http_stream_tests tests/HttpClientStreamTests.cpp src/net/HttpClient.cpp)
    target_include_directories(router_http_stream_tests PRIVATE src ${CMAKE_CURRENT_BINARY_DIR}/generated)
    target_link_libraries(router_http_stream_tests PRIVATE CURL::libcurl httplib::httplib)

    add_executable(router_credential_tests tests/CredentialStoreTests.cpp src/security/CredentialStore.cpp)
    target_include_directories(router_credential_tests PRIVATE src)
    if(WIN32)
        target_link_libraries(router_credential_tests PRIVATE crypt32)
    endif()

    add_executable(router_management_tests
        tests/ManagementFeaturesTests.cpp
        src/core/AccountSelector.cpp
        src/core/ConfigManager.cpp
        src/core/FailurePolicy.cpp
        src/core/MaintenanceManager.cpp
        src/core/RoutingManager.cpp
        src/security/CredentialStore.cpp
        src/storage/SQLiteDatabase.cpp
        src/storage/SQLiteDatabaseExtras.cpp
    )
    target_include_directories(router_management_tests PRIVATE src)
    target_link_libraries(router_management_tests PRIVATE unofficial::sqlite3::sqlite3 nlohmann_json::nlohmann_json)
    if(WIN32)
        target_link_libraries(router_management_tests PRIVATE crypt32)
    endif()

    add_executable(router_local_api_tests
        tests/LocalApiServerTests.cpp
        src/api/LocalApiServer.cpp
        src/api/LocalApiProviderAdmin.cpp
        src/api/OpenAICompat.cpp
        src/core/AccountManager.cpp
        src/core/AccountManagerLifecycle.cpp
        src/core/AccountSelector.cpp
        src/core/CompletionRouter.cpp
        src/core/CompletionStream.cpp
        src/core/ConfigManager.cpp
        src/core/FailurePolicy.cpp
        src/core/RoutingManager.cpp
        src/net/HttpClient.cpp
        src/providers/antigravity/AntigravityApiClient.cpp
        src/providers/antigravity/AntigravityCli.cpp
        src/providers/antigravity/AntigravityProvider.cpp
        src/providers/codex/CodexAppServerClient.cpp
        src/providers/codex/CodexCli.cpp
        src/providers/codex/CodexProvider.cpp
        src/providers/zai/ZaiClient.cpp
        src/providers/zai/ZaiProvider.cpp
        src/security/CredentialStore.cpp
        src/storage/SQLiteDatabase.cpp
        src/storage/SQLiteDatabaseExtras.cpp
        src/system/DuplexProcess.cpp
        src/system/ProcessRunner.cpp
    )
    target_include_directories(router_local_api_tests PRIVATE src ${CMAKE_CURRENT_BINARY_DIR}/generated)
    target_link_libraries(router_local_api_tests PRIVATE
        CURL::libcurl httplib::httplib unofficial::sqlite3::sqlite3 nlohmann_json::nlohmann_json
    )
    if(WIN32)
        target_link_libraries(router_local_api_tests PRIVATE crypt32)
    endif()

    add_executable(router_desktop_tests
        tests/DesktopProfileManagerTests.cpp
        src/desktop/DesktopAccountSwitcher.cpp
        src/desktop/DesktopProfileManager.cpp
        src/system/ProcessRunner.cpp
    )
    target_include_directories(router_desktop_tests PRIVATE src)

    set(ROUTER_TEST_TARGETS
        router_storage_tests router_selector_tests router_routing_tests router_openai_compat_tests
        router_provider_metadata_tests router_http_stream_tests router_credential_tests
        router_management_tests router_local_api_tests router_desktop_tests)
    foreach(test_target IN LISTS ROUTER_TEST_TARGETS)
        if(MSVC)
            target_compile_options(${test_target} PRIVATE /W4 /permissive-)
        else()
            target_compile_options(${test_target} PRIVATE -Wall -Wextra -Wpedantic)
        endif()
    endforeach()

    add_test(NAME sqlite_storage COMMAND router_storage_tests)
    add_test(NAME account_selector COMMAND router_selector_tests)
    add_test(NAME routing_manager COMMAND router_routing_tests)
    add_test(NAME openai_compat COMMAND router_openai_compat_tests)
    add_test(NAME provider_metadata COMMAND router_provider_metadata_tests)
    add_test(NAME http_stream COMMAND router_http_stream_tests)
    add_test(NAME credential_store COMMAND router_credential_tests)
    add_test(NAME management_features COMMAND router_management_tests)
    add_test(NAME local_api COMMAND router_local_api_tests)
    add_test(NAME desktop_manager COMMAND router_desktop_tests)
endif()
