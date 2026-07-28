include(GoogleTest)

add_executable(
    dvs_platform_component_tests
    AtomicFilePublisherTests.cpp
    CpuNv12FrameResourceTests.cpp
    FrameBudgetTests.cpp
    FrameMailboxTests.cpp
    PresentationAckMailboxTests.cpp
    SourceIdentityServiceTests.cpp
    SteadyDeadlineSchedulerTests.cpp
)
target_link_libraries(
    dvs_platform_component_tests
    PRIVATE
        dvs_platform_windows
        dvs::test_dependencies
)
target_include_directories(
    dvs_platform_component_tests
    PRIVATE
        "${PROJECT_SOURCE_DIR}/src/platform_windows/src"
)

dvs_apply_project_warnings(dvs_platform_component_tests)
set_target_properties(dvs_platform_component_tests PROPERTIES FOLDER "Tests/Component/Platform")

gtest_discover_tests(
    dvs_platform_component_tests
    TEST_PREFIX "platform."
    DISCOVERY_MODE PRE_TEST
    PROPERTIES
        LABELS "component;platform_windows"
        TIMEOUT 10
)

add_executable(
    dvs_graphics_device_broker_component_tests
    GpuTransferActorTests.cpp
    GraphicsDeviceBrokerTests.cpp
    ../ui/DualVideoSurfaceTests.cpp
    ../ui/RenderAckRelayTests.cpp
)
target_link_libraries(
    dvs_graphics_device_broker_component_tests
    PRIVATE
        dvs_platform_windows
        dvs_ui_qml
        dvs::qt_dependencies
        GTest::gtest
)
target_include_directories(
    dvs_graphics_device_broker_component_tests
    PRIVATE
        "${PROJECT_SOURCE_DIR}/src/platform_windows/src"
        "${PROJECT_SOURCE_DIR}/tests/component/ui"
)

dvs_apply_project_warnings(dvs_graphics_device_broker_component_tests)
set_target_properties(
    dvs_graphics_device_broker_component_tests
    PROPERTIES FOLDER "Tests/Component/Platform"
)

gtest_discover_tests(
    dvs_graphics_device_broker_component_tests
    TEST_PREFIX "platform."
    DISCOVERY_MODE PRE_TEST
    PROPERTIES
        LABELS "component;platform_windows;ui_qml;graphics;warp"
        TIMEOUT 20
        RUN_SERIAL TRUE
        ENVIRONMENT "QSG_RENDER_LOOP=threaded"
)
