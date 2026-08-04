include_guard(GLOBAL)

add_library(dvs_project_warnings INTERFACE)
target_compile_features(dvs_project_warnings INTERFACE cxx_std_20)
if(MSVC)
    target_compile_options(
        dvs_project_warnings
        INTERFACE
            /W4
            /WX
            /permissive-
            /Zc:__cplusplus
            /Zc:preprocessor
            /EHsc
            /utf-8
    )
else()
    target_compile_options(
        dvs_project_warnings
        INTERFACE
            -Wall
            -Wextra
            -Wpedantic
            -Werror
            -Wno-missing-field-initializers
    )
endif()

if(DVS_ENABLE_ASAN)
    string(REGEX REPLACE "(^| )[/-]RTC(1|s|u|su)($| )" " " CMAKE_CXX_FLAGS_DEBUG
                         "${CMAKE_CXX_FLAGS_DEBUG}")

    add_library(dvs_asan_options INTERFACE)
    target_compile_options(dvs_asan_options INTERFACE /fsanitize=address /Zi)
    target_link_options(dvs_asan_options INTERFACE /fsanitize=address /INCREMENTAL:NO)
    target_compile_definitions(dvs_asan_options INTERFACE DVS_ASAN_BUILD=1)
endif()

function(dvs_apply_project_warnings target)
    if(NOT TARGET "${target}")
        message(FATAL_ERROR "Cannot apply warnings to missing target: ${target}")
    endif()

    target_link_libraries("${target}" PRIVATE dvs_project_warnings)

    if(TARGET dvs_coverage_options)
        target_link_libraries("${target}" PRIVATE dvs_coverage_options)
    endif()

    if(TARGET dvs_asan_options)
        target_link_libraries("${target}" PRIVATE dvs_asan_options)
    endif()
endfunction()
