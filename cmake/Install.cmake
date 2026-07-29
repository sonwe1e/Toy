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

set(
    DVS_FFMPEG_RUNTIME_DIR
    "${PROJECT_SOURCE_DIR}/out/tools/ffmpeg/ffmpeg-8.1.2-essentials_build"
)
if(EXISTS "${DVS_FFMPEG_RUNTIME_DIR}/bin/ffmpeg.exe" AND
   EXISTS "${DVS_FFMPEG_RUNTIME_DIR}/bin/ffprobe.exe" AND
   EXISTS "${PROJECT_SOURCE_DIR}/out/tools/ffmpeg/.runtime-provenance.json")
    install(
        PROGRAMS
            "${DVS_FFMPEG_RUNTIME_DIR}/bin/ffmpeg.exe"
            "${DVS_FFMPEG_RUNTIME_DIR}/bin/ffprobe.exe"
        DESTINATION .
        COMPONENT Runtime
    )
    install(
        FILES "${PROJECT_SOURCE_DIR}/out/tools/ffmpeg/.runtime-provenance.json"
        DESTINATION licenses
        RENAME ffmpeg-runtime-provenance.json
        COMPONENT Runtime
    )
    install(
        FILES "${DVS_FFMPEG_RUNTIME_DIR}/LICENSE"
        DESTINATION licenses
        RENAME FFmpeg-GPL-3.0.txt
        COMPONENT Runtime
    )
    install(
        FILES "${DVS_FFMPEG_RUNTIME_DIR}/README.txt"
        DESTINATION licenses
        RENAME FFmpeg-runtime-build-info.txt
        COMPONENT Runtime
    )
endif()

set(DVS_VCPKG_SHARE_DIR "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share")
foreach(
    packageName
    IN ITEMS
        double-conversion
        ffmpeg
        md4c
        pcre2
        qtbase
        qtdeclarative
        qtlanguageserver
        qtshadertools
        qtsvg
        zlib
)
    set(packageCopyright "${DVS_VCPKG_SHARE_DIR}/${packageName}/copyright")
    if(EXISTS "${packageCopyright}")
        install(
            FILES "${packageCopyright}"
            DESTINATION licenses/vcpkg
            RENAME "${packageName}.txt"
            COMPONENT Runtime
        )
    endif()
endforeach()

if(EXISTS "${PROJECT_SOURCE_DIR}/assets")
    install(DIRECTORY "${PROJECT_SOURCE_DIR}/assets/" DESTINATION assets COMPONENT Runtime)
endif()

if(EXISTS "${PROJECT_SOURCE_DIR}/licenses")
    install(DIRECTORY "${PROJECT_SOURCE_DIR}/licenses/" DESTINATION licenses COMPONENT Runtime)
endif()
