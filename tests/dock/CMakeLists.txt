find_package(GTest CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(nlohmann_json_schema_validator CONFIG REQUIRED)
include(GoogleTest)

# Crashes on request, so crash_guard_tests.cpp can check the dock's crash handlers in a real crash.
add_executable(harbor_dock_crash_helper crash_helper.cpp)
target_link_libraries(harbor_dock_crash_helper PRIVATE harbor_dock_app wer)

add_executable(harbor_dock_tests
    codec_tests.cpp
    command_line_tests.cpp
    contract_tests.cpp
    crash_guard_tests.cpp
    environment_tests.cpp
    explorer_worker_tests.cpp
    file_log_tests.cpp
    guard_policy_tests.cpp
    line_framer_tests.cpp
    log_line_tests.cpp
    named_pipe_client_tests.cpp
    pipe_name_tests.cpp
    restore_record_tests.cpp
    taskbar_guard_tests.cpp
    win32_taskbar_tests.cpp
)
target_link_libraries(harbor_dock_tests PRIVATE
    harbor_dock_app
    nlohmann_json::nlohmann_json
    nlohmann_json_schema_validator::validator
    GTest::gtest
    GTest::gtest_main
)
target_compile_definitions(harbor_dock_tests PRIVATE
    HARBOR_CONTRACTS_DIR="${PROJECT_SOURCE_DIR}/contracts"
    HARBOR_CRASH_HELPER=L"$<TARGET_FILE:harbor_dock_crash_helper>"
)
add_dependencies(harbor_dock_tests harbor_dock_crash_helper)

# PRE_TEST discovers tests when ctest runs, not at build time, so cross-compiled builds
# (e.g. arm64 on an x64 machine) never try to execute a binary the host can't run.
# The lock keeps tests serial even under `ctest -j`: several use the same global test hotkey.
gtest_discover_tests(harbor_dock_tests
    DISCOVERY_MODE PRE_TEST
    PROPERTIES RESOURCE_LOCK harbor_desktop
)
