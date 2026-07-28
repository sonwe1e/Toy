include_guard(GLOBAL)

set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION ".")
set(CMAKE_INSTALL_UCRT_LIBRARIES TRUE)
include(InstallRequiredSystemLibraries)

install(
    TARGETS DualVideoStudio DualVideoStudioCli
    RUNTIME_DEPENDENCIES
        DIRECTORIES
            "$<TARGET_FILE_DIR:DualVideoStudio>"
            "$<TARGET_FILE_DIR:DualVideoStudioCli>"
        PRE_EXCLUDE_REGEXES
            "^api-ms-win-.*"
            "^ext-ms-.*"
        POST_EXCLUDE_REGEXES
            ".*[Ww][Ii][Nn][Dd][Oo][Ww][Ss][/\\\\][Ss][Yy][Ss][Tt][Ee][Mm]32[/\\\\].*"
    RUNTIME DESTINATION . COMPONENT Runtime
)

qt6_generate_deploy_script(
    TARGET DualVideoStudio
    NAME dvs_qt_runtime
    OUTPUT_SCRIPT dvs_qt_deploy_script
    CONTENT "
set(QT_DEPLOY_BIN_DIR \".\")
set(QT_DEPLOY_PLUGINS_DIR \".\")
set(QT_DEPLOY_QML_DIR \"qml\")
qt6_deploy_runtime_dependencies(
    EXECUTABLE \"$<TARGET_FILE:DualVideoStudio>\"
    GENERATE_QT_CONF
    NO_TRANSLATIONS
    DEPLOY_TOOL_OPTIONS
        --qmldir \"${PROJECT_SOURCE_DIR}/src/ui_qml/qml\"
)"
)
install(SCRIPT "${dvs_qt_deploy_script}" COMPONENT Runtime)

if(EXISTS "${PROJECT_SOURCE_DIR}/assets")
    install(DIRECTORY "${PROJECT_SOURCE_DIR}/assets/" DESTINATION assets COMPONENT Runtime)
endif()

if(EXISTS "${PROJECT_SOURCE_DIR}/licenses")
    install(DIRECTORY "${PROJECT_SOURCE_DIR}/licenses/" DESTINATION licenses COMPONENT Runtime)
endif()
