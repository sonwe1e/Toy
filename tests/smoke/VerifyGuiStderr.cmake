if(NOT DEFINED DVS_GUI_EXECUTABLE)
    message(FATAL_ERROR "DVS_GUI_EXECUTABLE is required.")
endif()

execute_process(
    COMMAND "${DVS_GUI_EXECUTABLE}" --ui-stderr-smoke
    RESULT_VARIABLE result
    OUTPUT_VARIABLE standardOutput
    ERROR_VARIABLE standardError
    TIMEOUT 5
)
if(NOT result EQUAL 0)
    message(
        FATAL_ERROR
        "The GUI stderr smoke failed with ${result}. "
        "stdout: ${standardOutput} stderr: ${standardError}"
    )
endif()
if(NOT standardError STREQUAL "DVS_GUI_STDERR_OK\n")
    message(
        FATAL_ERROR
        "The GUI did not write the expected redirected stderr marker. "
        "stdout: ${standardOutput} stderr: ${standardError}"
    )
endif()
