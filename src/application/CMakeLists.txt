add_library(
    dvs_application
    STATIC
        src/Alignment.cpp
        src/AlignmentCacheIdentity.cpp
        src/AlignmentWorkEstimator.cpp
        src/ComparisonExactness.cpp
        src/Module.cpp
        src/PlaybackCoordinator.cpp
        src/PrefetchScheduler.cpp
        src/SessionSnapshot.cpp
        src/WorkspaceCoordinator.cpp
)
add_library(dvs::application ALIAS dvs_application)

target_include_directories(
    dvs_application
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_link_libraries(dvs_application PUBLIC dvs_domain)

dvs_apply_project_warnings(dvs_application)
dvs_register_architecture_target(dvs_application)
set_target_properties(dvs_application PROPERTIES FOLDER "Modules")
