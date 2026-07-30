include(GoogleTest)

add_executable(
    dvs_ui_component_tests
    BadCaseExporterTests.cpp
    ReviewControllerTests.cpp
    ReviewPreferencesControllerTests.cpp
)
target_link_libraries(
    dvs_ui_component_tests
    PRIVATE
        dvs_ui_qml
        dvs::test_dependencies
)
target_include_directories(
    dvs_ui_component_tests
    PRIVATE "${PROJECT_SOURCE_DIR}/src/ui_qml/src"
)

dvs_apply_project_warnings(dvs_ui_component_tests)
set_target_properties(dvs_ui_component_tests PROPERTIES FOLDER "Tests/Component/UI")

gtest_discover_tests(
    dvs_ui_component_tests
    TEST_PREFIX "ui."
    DISCOVERY_MODE PRE_TEST
    PROPERTIES
        LABELS "component;ui_qml"
        TIMEOUT 10
)

add_executable(dvs_main_qml_contract_tests MainQmlContractTests.cpp)
target_link_libraries(
    dvs_main_qml_contract_tests
    PRIVATE
        dvs_ui_qml
        dvs::qt_dependencies
        GTest::gtest
)

dvs_apply_project_warnings(dvs_main_qml_contract_tests)
set_target_properties(
    dvs_main_qml_contract_tests
    PROPERTIES FOLDER "Tests/Component/UI"
)

gtest_discover_tests(
    dvs_main_qml_contract_tests
    TEST_PREFIX "ui."
    DISCOVERY_MODE PRE_TEST
    PROPERTIES
        LABELS "component;ui_qml"
        TIMEOUT 20
        RUN_SERIAL TRUE
        ENVIRONMENT "QSG_RENDER_LOOP=basic;QT_QUICK_CONTROLS_STYLE=Basic"
)

set(dvs_toolbar_combo_qml_test "${CMAKE_CURRENT_SOURCE_DIR}/qml/tst_toolbar_combo.qml")
foreach(dvs_qml_style IN ITEMS Basic Windows)
    add_test(
        NAME "ui.ToolbarCombo.${dvs_qml_style}"
        COMMAND
            "$<TARGET_FILE:Qt6::qmltestrunner>"
            -input "${dvs_toolbar_combo_qml_test}"
    )
    set_tests_properties(
        "ui.ToolbarCombo.${dvs_qml_style}"
        PROPERTIES
            LABELS "component;ui_qml"
            TIMEOUT 15
            RUN_SERIAL TRUE
            ENVIRONMENT "QT_QUICK_CONTROLS_STYLE=${dvs_qml_style}"
    )
endforeach()
