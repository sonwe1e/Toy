if(NOT DEFINED QMLFORMAT_EXECUTABLE OR NOT DEFINED FILE_LIST)
    message(FATAL_ERROR "QMLFORMAT_EXECUTABLE and FILE_LIST are required.")
endif()

file(STRINGS "${FILE_LIST}" qmlFiles)
foreach(qmlFile IN LISTS qmlFiles)
    execute_process(
        COMMAND "${QMLFORMAT_EXECUTABLE}" -w 4 "${qmlFile}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE formatted
        ERROR_VARIABLE diagnostics
        ENCODING UTF-8
    )
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "qmlformat failed for ${qmlFile}: ${diagnostics}")
    endif()

    file(READ "${qmlFile}" source)
    if(NOT source STREQUAL formatted)
        message(FATAL_ERROR "QML formatting differs: ${qmlFile}")
    endif()
endforeach()
