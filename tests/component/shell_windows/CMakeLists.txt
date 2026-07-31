include(GoogleTest)

add_executable(dvs_shell_invoke_probe ShellInvokeProbe.cpp)
dvs_apply_project_warnings(dvs_shell_invoke_probe)
set_target_properties(
    dvs_shell_invoke_probe
    PROPERTIES FOLDER "Tests/Component/ShellWindows"
)

add_executable(dvs_shell_component_tests ExplorerCommandSmokeTests.cpp)
target_include_directories(
    dvs_shell_component_tests
    PRIVATE "${PROJECT_SOURCE_DIR}/src/shell_windows"
)
target_link_libraries(
    dvs_shell_component_tests
    PRIVATE dvs::test_dependencies ole32 shell32 shlwapi
)
add_dependencies(dvs_shell_component_tests VCStationShell dvs_shell_invoke_probe)

dvs_apply_project_warnings(dvs_shell_component_tests)
set_target_properties(
    dvs_shell_component_tests
    PROPERTIES FOLDER "Tests/Component/ShellWindows"
)

gtest_discover_tests(
    dvs_shell_component_tests
    TEST_PREFIX "shell_windows."
    DISCOVERY_MODE PRE_TEST
    PROPERTIES
        LABELS "component;shell_windows"
        TIMEOUT 15
        ENVIRONMENT
            "DVS_SHELL_TEST_DLL=$<TARGET_FILE:VCStationShell>;DVS_SHELL_TEST_PROBE_EXE=$<TARGET_FILE:dvs_shell_invoke_probe>"
)
