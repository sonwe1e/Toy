cmake_minimum_required(VERSION 3.20)
project(DualVideoTool VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# Find Qt6
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

# Find FFmpeg via pkg-config
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
    libavcodec
    libavformat
    libavutil
    libswscale
    libswresample
)

set(SOURCES
    src/main.cpp

    # App
    src/app/MainWindow.cpp
    src/app/ShortcutManager.cpp

    # Video
    src/video/VideoDecoder.cpp
    src/video/VideoDecodeWorker.cpp
    src/video/FrameCache.cpp
    src/video/VideoEncoder.cpp

    # Playback
    src/playback/PlaybackEngine.cpp
    src/playback/ProxyBuilder.cpp

    # Smoke tests
    src/smoke/SmokeTest.cpp

    # Resources
    resources/resources.qrc

    # Export
    src/export/ClipExporter.cpp
    src/export/ClipQueue.cpp

    # UI
    src/ui/VideoView.cpp
    src/ui/TimelineWidget.cpp
    src/ui/ControlBar.cpp

    # Utils
    src/utils/FFmpegUtils.cpp
)

if(WIN32)
    list(APPEND SOURCES resources/app.rc)
endif()

add_executable(${PROJECT_NAME} WIN32 ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    PkgConfig::FFMPEG
)

# On Windows, add the MSYS2 UCRT64 bin to PATH for deployment
if(WIN32)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        WIN32_EXECUTABLE TRUE
    )
endif()
