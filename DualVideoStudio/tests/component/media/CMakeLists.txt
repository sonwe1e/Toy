include(GoogleTest)

add_executable(
    dvs_media_component_tests
    DirectFrameProviderTests.cpp
    FrameTimelineIndexTests.cpp
    MediaProbeTests.cpp
     ProbeTimelineContractTests.cpp
    SoftwareDecoderTests.cpp
)
target_link_libraries(
    dvs_media_component_tests
    PRIVATE
        dvs_media_ffmpeg
        dvs::test_dependencies
)
target_compile_definitions(
    dvs_media_component_tests
    PRIVATE DVS_MEDIA_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/tests/fixtures/media"
)
target_include_directories(
    dvs_media_component_tests
    PRIVATE
        "${PROJECT_SOURCE_DIR}/src/media_ffmpeg/src"
        "${PROJECT_SOURCE_DIR}/src/platform_windows/include"
)

dvs_apply_project_warnings(dvs_media_component_tests)
set_target_properties(dvs_media_component_tests PROPERTIES FOLDER "Tests/Component/Media")

gtest_discover_tests(
    dvs_media_component_tests
    TEST_PREFIX "media."
    DISCOVERY_MODE PRE_TEST
    PROPERTIES
        LABELS "component;media_ffmpeg;media_software"
        TIMEOUT 10
)
