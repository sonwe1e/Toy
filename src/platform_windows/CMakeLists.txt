add_library(
    dvs_platform_windows
    STATIC
        src/Module.cpp
        src/AtomicFilePublisher.cpp
        src/FrameBudget.cpp
        src/CpuNv12FrameResource.cpp
        src/D3d11GpuFrameBacking.cpp
        src/D3d11DualVideoRenderer.cpp
        src/D3d11RenderChannel.cpp
        src/FrameResourceFactory.cpp
        src/FrameMailbox.cpp
        src/GraphicsDeviceBroker.cpp
        src/GpuFramePair.cpp
        src/GpuFrameResource.cpp
        src/GpuTransferActor.cpp
        src/PresentationAckMailbox.cpp
        src/SourceIdentityService.cpp
        src/SteadyDeadlineScheduler.cpp
        src/WindowsPaths.cpp
)
add_library(dvs::platform_windows ALIAS dvs_platform_windows)

include(DvsHlsl)
dvs_compile_nv12_shaders(
    OUTPUT_HEADER_VARIABLE dvs_nv12_shader_header
    OUTPUT_INCLUDE_DIRECTORY_VARIABLE dvs_nv12_shader_include_directory
)
target_sources(dvs_platform_windows PRIVATE "${dvs_nv12_shader_header}")

target_include_directories(
    dvs_platform_windows
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE "${dvs_nv12_shader_include_directory}"
)
target_link_libraries(dvs_platform_windows PUBLIC dvs_application)
target_link_libraries(dvs_platform_windows PRIVATE d3d11 dxgi ole32 shell32 uuid)

dvs_apply_project_warnings(dvs_platform_windows)
dvs_register_architecture_target(dvs_platform_windows)
set_target_properties(dvs_platform_windows PROPERTIES FOLDER "Adapters")
