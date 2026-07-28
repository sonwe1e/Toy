add_library(dvs_jobs_ffmpeg STATIC src/Module.cpp)
add_library(dvs::jobs_ffmpeg ALIAS dvs_jobs_ffmpeg)

target_include_directories(
    dvs_jobs_ffmpeg
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_link_libraries(dvs_jobs_ffmpeg PRIVATE dvs_application dvs_platform_windows)

dvs_apply_project_warnings(dvs_jobs_ffmpeg)
dvs_register_architecture_target(dvs_jobs_ffmpeg)
set_target_properties(dvs_jobs_ffmpeg PROPERTIES FOLDER "Adapters")
