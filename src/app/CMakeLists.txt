add_executable(
    DualVideoStudio
    WIN32
    Main.cpp
    ReviewRuntime.cpp
    ReviewRuntime.h
    StartupFailureReporter.cpp
    StartupFailureReporter.h
)
add_executable(DualVideoStudioCli CliMain.cpp)

set(
    dvs_app_dependencies
    dvs_platform_windows
    dvs_media_ffmpeg
    dvs_persistence_json
    dvs_ui_qml
)

foreach(target DualVideoStudio DualVideoStudioCli)
    target_link_libraries(${target} PRIVATE ${dvs_app_dependencies})
    dvs_apply_project_warnings(${target})
    dvs_register_architecture_target(${target})
    set_target_properties(${target} PROPERTIES FOLDER "Application")
endforeach()

target_link_libraries(DualVideoStudio PRIVATE user32)

add_custom_command(
    TARGET DualVideoStudio
    POST_BUILD
    COMMAND
        "$<TARGET_FILE:Qt6::windeployqt>"
        --no-translations
        --qmldir "${PROJECT_SOURCE_DIR}/src/ui_qml/qml"
        "$<TARGET_FILE:DualVideoStudio>"
    VERBATIM
    COMMENT "Deploying Qt runtime for DualVideoStudio"
)
