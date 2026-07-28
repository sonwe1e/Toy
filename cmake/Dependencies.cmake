include_guard(GLOBAL)

macro(dvs_resolve_dependencies)
    find_package(Qt6 6.11 REQUIRED COMPONENTS Core Gui Qml Quick ShaderTools)

    find_package(nlohmann_json CONFIG REQUIRED)

    # vcpkg's FFmpeg port supplies a wrapper around its versioned FindFFMPEG module.  Using it
    # avoids a separate host pkg-config requirement on clean Windows developer machines.
    find_package(FFMPEG REQUIRED)

    add_library(dvs_qt_dependencies INTERFACE)
    add_library(dvs::qt_dependencies ALIAS dvs_qt_dependencies)
    target_link_libraries(
        dvs_qt_dependencies
        INTERFACE
            Qt6::Core
            Qt6::Gui
            Qt6::Qml
            Qt6::Quick
            Qt6::ShaderTools
    )

    add_library(dvs_ffmpeg_dependencies INTERFACE)
    add_library(dvs::ffmpeg_dependencies ALIAS dvs_ffmpeg_dependencies)
    target_include_directories(dvs_ffmpeg_dependencies SYSTEM INTERFACE ${FFMPEG_INCLUDE_DIRS})
    target_link_libraries(dvs_ffmpeg_dependencies INTERFACE ${FFMPEG_LIBRARIES})

    add_library(dvs_json_dependencies INTERFACE)
    add_library(dvs::json_dependencies ALIAS dvs_json_dependencies)
    target_link_libraries(dvs_json_dependencies INTERFACE nlohmann_json::nlohmann_json)

    if(BUILD_TESTING)
        find_package(GTest CONFIG REQUIRED)
        add_library(dvs_test_dependencies INTERFACE)
        add_library(dvs::test_dependencies ALIAS dvs_test_dependencies)
        target_link_libraries(dvs_test_dependencies INTERFACE GTest::gtest GTest::gtest_main)
    endif()
endmacro()
