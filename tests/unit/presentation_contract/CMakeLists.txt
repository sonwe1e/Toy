include(GoogleTest)

add_executable(
    dvs_presentation_contract_unit_tests
    ComparisonContractTests.cpp
)
target_link_libraries(
    dvs_presentation_contract_unit_tests
    PRIVATE
        dvs_presentation_contract
        dvs::test_dependencies
)

dvs_apply_project_warnings(dvs_presentation_contract_unit_tests)
set_target_properties(
    dvs_presentation_contract_unit_tests
    PROPERTIES FOLDER "Tests/Unit/PresentationContract"
)

dvs_add_gtest(
    TARGET dvs_presentation_contract_unit_tests
    TEST_PREFIX "presentation."
    LAYER unit
    MODULE presentation_contract
    TIMEOUT 10
)
