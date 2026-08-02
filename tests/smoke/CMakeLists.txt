dvs_add_test(
    NAME app.startup-check
    LAYER e2e
    MODULE app
    TIMEOUT 10
    COMMAND $<TARGET_FILE:VCStationCli> --startup-check
)

dvs_add_test(
    NAME quality.release-contract
    LAYER e2e
    MODULE app
    TIMEOUT 10
    COMMAND
        ${CMAKE_COMMAND}
        "-DDVS_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
        "-DDVS_BINARY_DIR=${PROJECT_BINARY_DIR}"
        "-DDVS_EXPECTED_VERSION=${PROJECT_VERSION}"
        "-DDVS_EXPECTED_SHELL=${DVS_SHELL_BINARY_NAME}"
        -P
        "${CMAKE_CURRENT_SOURCE_DIR}/VerifyReleaseContract.cmake"
)

dvs_add_test(
    NAME app.gui-pe-subsystem
    LAYER e2e
    MODULE app
    TIMEOUT 10
    COMMAND
        ${CMAKE_COMMAND}
        "-DDVS_PE_EXECUTABLE=$<TARGET_FILE:VCStation>"
        -DDVS_PE_EXPECTED_SUBSYSTEM=WINDOWS_GUI
        -P
        "${PROJECT_SOURCE_DIR}/cmake/VerifyPeSubsystem.cmake"
)

dvs_add_test(
    NAME app.gui-stderr-redirection
    LAYER e2e
    MODULE app
    TIMEOUT 10
    COMMAND
        ${CMAKE_COMMAND}
        "-DDVS_GUI_EXECUTABLE=$<TARGET_FILE:VCStation>"
        -P
        "${CMAKE_CURRENT_SOURCE_DIR}/VerifyGuiStderr.cmake"
)

dvs_add_test(
    NAME app.cli-pe-subsystem
    LAYER e2e
    MODULE app
    TIMEOUT 10
    COMMAND
        ${CMAKE_COMMAND}
        "-DDVS_PE_EXECUTABLE=$<TARGET_FILE:VCStationCli>"
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
        "-DDVS_GUI_EXECUTABLE=$<TARGET_FILE:VCStation>"
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
        "-DDVS_CLI_EXECUTABLE=$<TARGET_FILE:VCStationCli>"
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
        $<TARGET_FILE:VCStation>
        --ui-smoke
        "${PROJECT_SOURCE_DIR}/tests/fixtures/media/h264_a_320x180_30fps_12.mp4"
        "${PROJECT_SOURCE_DIR}/tests/fixtures/media/h264_b_160x90_30fps_12.mp4"
)

dvs_add_test(
    NAME app.ui-single-source-smoke
    LAYER e2e
    MODULE app
    TIMEOUT 40
    COMMAND
        $<TARGET_FILE:VCStation>
        --ui-smoke
        "${PROJECT_SOURCE_DIR}/tests/fixtures/media/h264_a_320x180_30fps_12.mp4"
)

dvs_add_test(
    NAME app.ui-three-source-smoke
    LAYER e2e
    MODULE app
    TIMEOUT 40
    COMMAND
        $<TARGET_FILE:VCStation>
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
    COMMAND $<TARGET_FILE:VCStation> --ui-smoke
)

dvs_add_test(
    NAME app.ui-shutdown-during-open-smoke
    LAYER e2e
    MODULE app
    TIMEOUT 9
    COMMAND
        $<TARGET_FILE:VCStation>
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
        $<TARGET_FILE:VCStationCli>
        --compare
        "${PROJECT_SOURCE_DIR}/tests/fixtures/media/h264_a_320x180_30fps_12.mp4"
        "${PROJECT_SOURCE_DIR}/tests/fixtures/media/h264_b_160x90_30fps_12.mp4"
        --frame
        11
)

dvs_add_test(
    NAME package.msi-install-association
    LAYER packaged
    MODULE app
    TIMEOUT 180
    COMMAND
        powershell.exe
        -NoLogo
        -NoProfile
        -ExecutionPolicy Bypass
        -File "${CMAKE_CURRENT_SOURCE_DIR}/VerifyMsiPackage.ps1"
        -MsiPath "${PROJECT_SOURCE_DIR}/out/package/msi/VCStation-${PROJECT_VERSION}-windows-x64.msi"
        -ExpectedVersion "${PROJECT_VERSION}"
        -ExpectedShellBinaryName "${DVS_SHELL_BINARY_NAME}"
        -ProbeFixture
        "${PROJECT_SOURCE_DIR}/tests/fixtures/media/h264_a_320x180_30fps_12.mp4"
)
set_tests_properties(
    package.msi-install-association
    PROPERTIES
        RUN_SERIAL TRUE
        RESOURCE_LOCK vcstation-machine-install
)

if(DVS_PREVIOUS_RELEASE_MSI)
    dvs_add_test(
        NAME package.msi-upgrade-from-1.1.0
        LAYER packaged
        MODULE app
        TIMEOUT 240
        COMMAND
            powershell.exe
            -NoLogo
            -NoProfile
            -ExecutionPolicy Bypass
            -File "${CMAKE_CURRENT_SOURCE_DIR}/VerifyMsiPackage.ps1"
            -MsiPath "${PROJECT_SOURCE_DIR}/out/package/msi/VCStation-${PROJECT_VERSION}-windows-x64.msi"
            -ExpectedVersion "${PROJECT_VERSION}"
            -ExpectedShellBinaryName "${DVS_SHELL_BINARY_NAME}"
            -ProbeFixture
            "${PROJECT_SOURCE_DIR}/tests/fixtures/media/h264_a_320x180_30fps_12.mp4"
            -PreviousMsiPath "${DVS_PREVIOUS_RELEASE_MSI}"
    )
    set_tests_properties(
        package.msi-upgrade-from-1.1.0
        PROPERTIES
            RUN_SERIAL TRUE
            RESOURCE_LOCK vcstation-machine-install
    )
endif()

dvs_add_test(
    NAME app.shutdown-soak
    LAYER soak
    MODULE app
    TIMEOUT 240
    COMMAND
        powershell.exe
        -NoLogo
        -NoProfile
        -ExecutionPolicy Bypass
        -File "${CMAKE_CURRENT_SOURCE_DIR}/ShutdownSoak.ps1"
        -Executable "$<TARGET_FILE:VCStation>"
        -SourceA
        "${PROJECT_SOURCE_DIR}/tests/fixtures/media/h264_a_320x180_30fps_12.mp4"
        -SourceB
        "${PROJECT_SOURCE_DIR}/tests/fixtures/media/h264_b_160x90_30fps_12.mp4"
        -Iterations 20
)
