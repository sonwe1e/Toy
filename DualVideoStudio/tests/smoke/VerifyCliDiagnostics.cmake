if(NOT DEFINED DVS_CLI_EXECUTABLE OR NOT DEFINED DVS_SOURCE_A OR NOT DEFINED DVS_SOURCE_B)
    message(FATAL_ERROR "DVS_CLI_EXECUTABLE, DVS_SOURCE_A, and DVS_SOURCE_B are required.")
endif()

get_filename_component(_vfrFixtureDir "${DVS_SOURCE_A}" DIRECTORY)
set(_vfrFixture "${_vfrFixtureDir}/h264_vfr_320x180_12.mp4")
if(NOT EXISTS "${_vfrFixture}")
    message(FATAL_ERROR "VFR fixture was not found at ${_vfrFixture}.")
endif()

execute_process(
    COMMAND "${DVS_CLI_EXECUTABLE}" --probe "${_vfrFixture}"
    RESULT_VARIABLE vfrProbeResult
    OUTPUT_VARIABLE vfrProbeOutput
    ERROR_VARIABLE vfrProbeError
    TIMEOUT 5
)
if(NOT vfrProbeResult EQUAL 0 OR
   NOT vfrProbeOutput MATCHES "rate=vfr" OR
   NOT vfrProbeOutput MATCHES "frames=12" OR
   vfrProbeError MATCHES "error=")
    message(
        FATAL_ERROR
        "CLI VFR probe diagnostics were incomplete (${vfrProbeResult}). "
        "stdout: ${vfrProbeOutput} stderr: ${vfrProbeError}"
    )
endif()
unset(_vfrFixture)
unset(_vfrFixtureDir)

execute_process(
    COMMAND "${DVS_CLI_EXECUTABLE}" --probe "${DVS_SOURCE_A}"
    RESULT_VARIABLE probeResult
    OUTPUT_VARIABLE probeOutput
    ERROR_VARIABLE probeError
    TIMEOUT 5
)
if(NOT probeResult EQUAL 0 OR
   NOT probeOutput MATCHES "codec=h264" OR
   NOT probeOutput MATCHES "extent=320x180" OR
   NOT probeOutput MATCHES "rate=30/1" OR
   NOT probeOutput MATCHES "frames=12")
    message(
        FATAL_ERROR
        "CLI probe diagnostics were incomplete (${probeResult}). "
        "stdout: ${probeOutput} stderr: ${probeError}"
    )
endif()

execute_process(
    COMMAND
        "${DVS_CLI_EXECUTABLE}"
        --compare
        "${DVS_SOURCE_A}"
        "${DVS_SOURCE_B}"
        --frame
        invalid
    RESULT_VARIABLE invalidResult
    OUTPUT_VARIABLE invalidOutput
    ERROR_VARIABLE invalidError
    TIMEOUT 5
)
if(invalidResult EQUAL 0 OR NOT invalidError MATCHES "error=invalid-frame-id")
    message(
        FATAL_ERROR
        "CLI invalid-frame diagnostics were not machine-readable (${invalidResult}). "
        "stdout: ${invalidOutput} stderr: ${invalidError}"
    )
endif()
