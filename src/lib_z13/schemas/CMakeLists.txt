project(fbs_schemas)

# The schemas more than one library uses. Those with one user live beside it (see
# z13_generate_schemas); they include these, so the directory is passed on.
set(FBS_DIR ${PROJECT_SOURCE_DIR}/fbs)
set(Z13_SHARED_SCHEMA_DIR ${FBS_DIR} PARENT_SCOPE)

set(
    FBS_SCHEMAS
    ${FBS_DIR}/actions.fbs
    ${FBS_DIR}/building_tuning.fbs
    ${FBS_DIR}/common.fbs
    ${FBS_DIR}/door_tuning.fbs
    ${FBS_DIR}/geometry.fbs
    ${FBS_DIR}/input_config.fbs
    ${FBS_DIR}/net_tuning.fbs
    ${FBS_DIR}/physics_tuning.fbs
    ${FBS_DIR}/power_tuning.fbs
    ${FBS_DIR}/primitives.fbs
    ${FBS_DIR}/settings.fbs
)

# settings.fbs includes lib_core's core_settings.fbs, whose generated header lives with lib_core.
z13_generate_schemas(FBS_SCHEMAS_HEADERS SCHEMAS ${FBS_SCHEMAS} INCLUDE ${CMAKE_SOURCE_DIR}/src/lib_core/fbs)

z13_add_library(${PROJECT_NAME} INTERFACE PUBLIC zodiac13::core flatbuffers::flatbuffers)
add_dependencies(${PROJECT_NAME} FBS_SCHEMAS_HEADERS)

target_include_directories(
  ${PROJECT_NAME}
  INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/FBS_SCHEMAS_HEADERS>
  $<INSTALL_INTERFACE:${Z13_INSTALL_INCLUDE_DIR}>
)
