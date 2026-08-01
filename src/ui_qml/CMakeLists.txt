add_library(
    dvs_ui_qml
    STATIC
        include/dvs/ui/ComparisonSurface.h
        include/dvs/ui/ReviewController.h
        include/dvs/ui/ReviewPreferencesController.h
        include/dvs/ui/ReviewShellController.h
        include/dvs/ui/SourceListModel.h
        include/dvs/ui/WorkspaceController.h
        src/BadCaseExporter.cpp
        src/BadCaseExporter.h
        src/DesktopApplication.cpp
        src/DirectCompare.cpp
        src/ComparisonSurface.cpp
        src/GraphicsBackend.cpp
        src/Module.cpp
        src/RenderAckRelay.cpp
        src/ReviewController.cpp
        src/ReviewPreferencesController.cpp
        src/ReviewShellController.cpp
        src/SourceListModel.cpp
        src/WorkspaceController.cpp
)
add_library(dvs::ui_qml ALIAS dvs_ui_qml)

target_include_directories(
    dvs_ui_qml
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_link_libraries(
    dvs_ui_qml
    PUBLIC dvs_application dvs_platform_windows dvs::qt_dependencies
)

qt6_add_resources(dvs_ui_qml_resource_sources dvs_ui_qml_resources.qrc)
target_sources(dvs_ui_qml PRIVATE ${dvs_ui_qml_resource_sources})

set_target_properties(dvs_ui_qml PROPERTIES AUTOMOC ON)

dvs_apply_project_warnings(dvs_ui_qml)
dvs_register_architecture_target(dvs_ui_qml)
set_target_properties(dvs_ui_qml PROPERTIES FOLDER "Adapters")
