dvs_add_test(
    NAME app.startup-check
    LAYER e2e
    MODULE app
    TIMEOUT 10
    COMMAND $<TARGET_FILE:DualVideoStudioCli> --startup-check
)

dvs_add_test(
    NAME app.gui-pe-subsystem
    LAYER e2e
    MODULE app
    TIMEOUT 10
    COMMAND
        ${CMAKE_COMMAND}
        "-DDVS_PE_EXECUTABLE=$<TARGET_FILE:DualVideoStudio>"
        -DDVS_PE_EXPECTED_SUBSYSTEM=WINDOWS_GUI
        -P
        "${PROJECT_SOURCE_DIR}/cmake/VerifyPeSubsystem.cmake"
)

dvs_add_test(
    NAME app.cli-pe-subsystem
    LAYER e2e
    MODULE app
    TIMEOUT 10
    COMMAND
        ${CMAKE_COMMAND}
        "-DDVS_PE_EXECUTABLE=$<TARGET_FILE:DualVideoStudioCli>"
        -DDVS_PE_EXPECTED_SUBSYSTEM=WINDOWS_CUI
        -P
        "${PROJECT_SOURCE_DIR}/cmake/VerifyPeSubsystem.cmake"
)

dvs_add_test(
    NAME app.fatal-startup-log
    LAYER e2e
    MODULE app
    TIMEOUT 10
    COMMAND
        ${CMAKE_COMMAND}
        "-DDVS_GUI_EXECUTABLE=$<TARGET_FILE:DualVideoStudio>"
        "-DDVS_TEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/fatal-startup"
        -P
        "${CMAKE_CURRENT_SOURCE_DIR}/VerifyFatalStartup.cmake"
)

dvs_add_test(
    NAME app.cli-diagnostics
    LAYER e2e
    MODULE app
    TIMEOUT 10
    COMMAND
        ${CMAKE_COMMAND}
        "-DDVS_CLI_EXECUTABLE=$<TARGET_FILE:DualVideoStudioCli>"
        "-DDVS_SOURCE_A=${PROJECT_SOURCE_DIR}/tests/fixtures/media/h264_a_320x180_30fps_12.mp4"
        "-DDVS_SOURCE_B=${PROJECT_SOURCE_DIR}/tests/fixtures/media/h264_b_160x90_30fps_12.mp4"
        -P
        "${CMAKE_CURRENT_SOURCE_DIR}/VerifyCliDiagnostics.cmake"
)

dvs_add_test(
    NAME app.ui-shell-smoke
    LAYER e2e
    MODULE app
    TIMEOUT 40
    COMMAND
        $<TARGET_FILE:DualVideoStudio>
        --ui-smoke
        "${PROJECT_SOURCE_DIR}/tests/fixtures/media/h264_a_320x180_30fps_12.mp4"
        "${PROJECT_SOURCE_DIR}/tests/fixtures/media/h264_b_160x90_30fps_12.mp4"
)

dvs_add_test(
    NAME app.ui-three-source-smoke
    LAYER e2e
    MODULE app
    TIMEOUT 40
    COMMAND
        $<TARGET_FILE:DualVideoStudio>
        --ui-smoke
        "${PROJECT_SOURCE_DIR}/tests/fixtures/media/h264_a_320x180_30fps_12.mp4"
        "${PROJECT_SOURCE_DIR}/tests/fixtures/media/h264_b_160x90_30fps_12.mp4"
        "${PROJECT_SOURCE_DIR}/tests/fixtures/media/h265_a_320x180_30fps_12.mp4"
)

# Exercises shutdown immediately after the WARP scene graph becomes ready. The timeout includes
# process startup and therefore leaves a small allowance above the runtime's seven-second bound.
dvs_add_test(
    NAME app.ui-immediate-shutdown-smoke
    LAYER e2e
    MODULE app
    TIMEOUT 9
    COMMAND $<TARGET_FILE:DualVideoStudio> --ui-smoke
)

dvs_add_test(
    NAME app.ui-shutdown-during-open-smoke
    LAYER e2e
    MODULE app
    TIMEOUT 9
    COMMAND
        $<TARGET_FILE:DualVideoStudio>
        --ui-shutdown-smoke
        "${PROJECT_SOURCE_DIR}/tests/fixtures/media/h264_a_320x180_30fps_12.mp4"
        "${PROJECT_SOURCE_DIR}/tests/fixtures/media/h264_b_160x90_30fps_12.mp4"
)

dvs_add_test(
    NAME app.direct-frame-smoke
    LAYER e2e
    MODULE app
    TIMEOUT 10
    COMMAND
        $<TARGET_FILE:DualVideoStudioCli>
        --compare
        "${PROJECT_SOURCE_DIR}/tests/fixtures/media/h264_a_320x180_30fps_12.mp4"
        "${PROJECT_SOURCE_DIR}/tests/fixtures/media/h264_b_160x90_30fps_12.mp4"
        --frame
        11
)
