configure_file(
    VCStationShell.rc.in
    "${CMAKE_CURRENT_BINARY_DIR}/VCStationShell.rc"
    @ONLY
)

add_library(
    VCStationShell
    SHARED
    ClassFactory.cpp
    ExplorerCommand.cpp
    ExplorerCommand.h
    ExplorerCommandSupport.cpp
    ExplorerCommandSupport.h
    VCStationShell.def
    "${CMAKE_CURRENT_BINARY_DIR}/VCStationShell.rc"
)
target_link_libraries(VCStationShell PRIVATE ole32 shell32 shlwapi)

dvs_apply_project_warnings(VCStationShell)
dvs_register_architecture_target(VCStationShell)
set_target_properties(
    VCStationShell
    PROPERTIES
        FOLDER "Application"
        PREFIX ""
        OUTPUT_NAME "${DVS_SHELL_BINARY_BASENAME}"
)
