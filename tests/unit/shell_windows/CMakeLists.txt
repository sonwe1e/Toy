include(GoogleTest)

add_executable(
    dvs_shell_support_unit_tests
    ExplorerCommandSupportTests.cpp
    "${PROJECT_SOURCE_DIR}/src/shell_windows/ExplorerCommandSupport.cpp"
)
target_include_directories(
    dvs_shell_support_unit_tests
    PRIVATE "${PROJECT_SOURCE_DIR}/src/shell_windows"
)
target_link_libraries(dvs_shell_support_unit_tests PRIVATE dvs::test_dependencies shell32)

dvs_apply_project_warnings(dvs_shell_support_unit_tests)
set_target_properties(
    dvs_shell_support_unit_tests
    PROPERTIES FOLDER "Tests/Unit/ShellWindows"
)

dvs_add_gtest(
    TARGET dvs_shell_support_unit_tests
    TEST_PREFIX "shell_windows."
    LAYER unit
    MODULE shell_windows
    TIMEOUT 10
)
