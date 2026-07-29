include(GoogleTest)

add_executable(
    dvs_application_unit_tests
    AlignmentTests.cpp
    AlignmentWorkEstimatorTests.cpp
    ContractCompileTests.cpp
    ComparisonExactnessTests.cpp
    PlaybackCoordinatorTests.cpp
    PrefetchSchedulerTests.cpp
    WorkspaceCoordinatorTests.cpp
)
target_link_libraries(
    dvs_application_unit_tests
    PRIVATE
        dvs_application
        dvs::test_dependencies
)

dvs_apply_project_warnings(dvs_application_unit_tests)
set_target_properties(dvs_application_unit_tests PROPERTIES FOLDER "Tests/Unit/Application")

gtest_discover_tests(
    dvs_application_unit_tests
    TEST_PREFIX "application."
    DISCOVERY_MODE PRE_TEST
    PROPERTIES
        LABELS "unit;application"
        TIMEOUT 10
)
