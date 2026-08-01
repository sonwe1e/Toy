include(GoogleTest)

add_executable(
    dvs_persistence_json_unit_tests
    CommitSemanticsTests.cpp
    SettingsRepositoryTests.cpp
)
target_link_libraries(
    dvs_persistence_json_unit_tests
    PRIVATE
        dvs_persistence_json
        dvs::test_dependencies
)
target_include_directories(
    dvs_persistence_json_unit_tests
    PRIVATE
        "${PROJECT_SOURCE_DIR}/src/persistence_json/src"
        "${PROJECT_SOURCE_DIR}/src/platform_windows/include"
)

dvs_apply_project_warnings(dvs_persistence_json_unit_tests)
set_target_properties(
    dvs_persistence_json_unit_tests
    PROPERTIES FOLDER "Tests/Unit/PersistenceJson"
)

gtest_discover_tests(
    dvs_persistence_json_unit_tests
    TEST_PREFIX "persistence_json."
    DISCOVERY_MODE PRE_TEST
    PROPERTIES
        LABELS "unit;persistence_json"
        TIMEOUT 10
)
