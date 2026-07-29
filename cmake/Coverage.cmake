include_guard(GLOBAL)

if(DVS_ENABLE_COVERAGE)
    add_library(dvs_coverage_options INTERFACE)
    target_compile_options(dvs_coverage_options INTERFACE /Zi)
    target_link_options(
        dvs_coverage_options
        INTERFACE
            /DEBUG:FULL
            /INCREMENTAL:NO
            /OPT:NOREF
            /OPT:NOICF
            /PROFILE
    )
    target_compile_definitions(dvs_coverage_options INTERFACE DVS_COVERAGE_BUILD=1)
endif()
