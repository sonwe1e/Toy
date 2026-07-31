find_program(DVS_POWERSHELL_EXECUTABLE NAMES pwsh powershell REQUIRED)

dvs_add_test(
    NAME hardware.d3d11va_decode
    LAYER hardware
    MODULE media_ffmpeg
    COMMAND
        $<TARGET_FILE:dvs_media_component_tests>
        --gtest_filter=SoftwareDecoderHardwareTests.*
)
set_tests_properties(hardware.d3d11va_decode PROPERTIES RUN_SERIAL TRUE TIMEOUT 60)

dvs_add_test(
    NAME hardware.d3d11_zero_copy
    LAYER hardware
    MODULE platform_windows
    COMMAND
        $<TARGET_FILE:dvs_graphics_device_broker_component_tests>
        --gtest_filter=GpuTransferActorTests.PublishesDecoderOwnedNv12ArraySlicesWithoutACopy
)
set_tests_properties(hardware.d3d11_zero_copy PROPERTIES RUN_SERIAL TRUE TIMEOUT 60)

dvs_add_test(
    NAME performance.1080p60-1source
    LAYER performance
    MODULE app
    COMMAND
        "${DVS_POWERSHELL_EXECUTABLE}"
        -NoLogo
        -NoProfile
        -ExecutionPolicy
        Bypass
        -File
        "${PROJECT_SOURCE_DIR}/tools/run-performance-gate.ps1"
        -Profile
        1080p60-1source
        -Executable
        $<TARGET_FILE:VCStation>
        -DurationSeconds
        300
)
set_tests_properties(
    performance.1080p60-1source
    PROPERTIES
        RUN_SERIAL TRUE
        TIMEOUT 480
)

dvs_add_test(
    NAME performance.1080p60
    LAYER performance
    MODULE app
    COMMAND
        "${DVS_POWERSHELL_EXECUTABLE}"
        -NoLogo
        -NoProfile
        -ExecutionPolicy
        Bypass
        -File
        "${PROJECT_SOURCE_DIR}/tools/run-performance-gate.ps1"
        -Profile
        1080p60
        -Executable
        $<TARGET_FILE:VCStation>
        -DurationSeconds
        300
)
set_tests_properties(
    performance.1080p60
    PROPERTIES
        RUN_SERIAL TRUE
        TIMEOUT 480
)

foreach(profile IN ITEMS 1080p120-1source 1080p120-2source 1080p120-3source)
    dvs_add_test(
        NAME performance.${profile}
        LAYER performance
        MODULE app
        COMMAND
            "${DVS_POWERSHELL_EXECUTABLE}"
            -NoLogo
            -NoProfile
            -ExecutionPolicy
            Bypass
            -File
            "${PROJECT_SOURCE_DIR}/tools/run-performance-gate.ps1"
            -Profile
            "${profile}"
            -Executable
            $<TARGET_FILE:VCStation>
            -DurationSeconds
            60
    )
    set_tests_properties(
        performance.${profile}
        PROPERTIES
            RUN_SERIAL TRUE
            TIMEOUT 180
    )
endforeach()
