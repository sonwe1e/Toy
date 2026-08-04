add_library(
    dvs_presentation_contract
    STATIC
        src/ComparisonContract.cpp
)
add_library(dvs::presentation_contract ALIAS dvs_presentation_contract)

target_include_directories(
    dvs_presentation_contract
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

dvs_apply_project_warnings(dvs_presentation_contract)
dvs_register_architecture_target(dvs_presentation_contract)
set_target_properties(dvs_presentation_contract PROPERTIES FOLDER "Modules")
