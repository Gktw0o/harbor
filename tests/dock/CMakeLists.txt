find_package(GTest CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
include(GoogleTest)

add_executable(harbor_dock_tests
    codec_tests.cpp
    contract_tests.cpp
    line_framer_tests.cpp
    pipe_name_tests.cpp
)
target_link_libraries(harbor_dock_tests PRIVATE
    harbor_dock_core
    nlohmann_json::nlohmann_json
    GTest::gtest
    GTest::gtest_main
)
target_compile_definitions(harbor_dock_tests PRIVATE HARBOR_CONTRACTS_DIR="${PROJECT_SOURCE_DIR}/contracts")

# PRE_TEST discovers tests when ctest runs, not at build time, so cross-compiled builds
# (e.g. arm64 on an x64 machine) never try to execute a binary the host can't run.
gtest_discover_tests(harbor_dock_tests DISCOVERY_MODE PRE_TEST)
