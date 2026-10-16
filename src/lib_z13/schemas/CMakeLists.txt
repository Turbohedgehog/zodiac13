project(fbs_schemas)

find_package(flatbuffers CONFIG REQUIRED)

find_path(FLATBUFFERS_CMAKE_DIR BuildFlatBuffers.cmake
          HINTS ${flatbuffers_DIR} "${flatbuffers_DIR}/../../../share/flatbuffers")
include(${FLATBUFFERS_CMAKE_DIR}/BuildFlatBuffers.cmake)

set(FBS_DIR ${PROJECT_SOURCE_DIR}/fbs)

set(
    FBS_SCHEMAS
    ${FBS_DIR}/actions.fbs
    ${FBS_DIR}/blueprint.fbs
    ${FBS_DIR}/building.fbs
    ${FBS_DIR}/building_tuning.fbs
    ${FBS_DIR}/common.fbs
    ${FBS_DIR}/discovery.fbs
    ${FBS_DIR}/geometry.fbs
    ${FBS_DIR}/input_config.fbs
    ${FBS_DIR}/net.fbs
    ${FBS_DIR}/net_tuning.fbs
    ${FBS_DIR}/physics_tuning.fbs
    ${FBS_DIR}/primitives.fbs
    ${FBS_DIR}/settings.fbs
)

set(
    GEN_FLAT_FLAGS
    "--bfbs-gen-embed"
    "--scoped-enums"
    "--reflect-names"
    "--gen-name-strings"
    "--gen-object-api"
    "--gen-compare"
    "--cpp-std" "c++17"
)

flatbuffers_generate_headers(
    TARGET FBS_SCHEMAS_HEADERS
    SCHEMAS ${FBS_SCHEMAS}
    BINARY_SCHEMAS_DIR "${CMAKE_CURRENT_BINARY_DIR}/bfbs"
    INCLUDE_DIR ${FBS_DIR}
    INCLUDE ${CMAKE_SOURCE_DIR}/src/lib_core/fbs
    FLAGS ${GEN_FLAT_FLAGS}
)

set(FBS_INCLUDE_DIRS ${FBS_DIR} ${CMAKE_SOURCE_DIR}/src/lib_core/fbs)

# Appends to `out_var` the schemas `schema` includes, directly or through others.
function(z13_fbs_includes schema out_var)
  set(found ${${out_var}})
  file(STRINGS ${schema} include_lines REGEX "^include \"[^\"]+\"")
  foreach(line IN LISTS include_lines)
    string(REGEX REPLACE "^include \"([^\"]+)\".*" "\\1" included "${line}")
    foreach(dir IN LISTS FBS_INCLUDE_DIRS)
      if(EXISTS ${dir}/${included} AND NOT ${dir}/${included} IN_LIST found)
        list(APPEND found ${dir}/${included})
        z13_fbs_includes(${dir}/${included} found)
        break()
      endif()
    endforeach()
  endforeach()
  set(${out_var} ${found} PARENT_SCOPE)
endfunction()

# flatc's rules depend only on their own schema, but --bfbs-gen-embed embeds what a schema
# includes too: each output also depends on the schemas its schema includes, and no others, so
# a schema nobody includes (net.fbs) regenerates its own header only.
foreach(schema IN LISTS FBS_SCHEMAS)
  get_filename_component(schema_name ${schema} NAME_WE)
  set(included "")
  z13_fbs_includes(${schema} included)
  if(included)
    add_custom_command(
      OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/FBS_SCHEMAS_HEADERS/${schema_name}_generated.h
      APPEND DEPENDS ${included})
    add_custom_command(
      OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/bfbs/${schema_name}.bfbs
      APPEND DEPENDS ${included})
  endif()
endforeach()
# The include lines are read here, so a schema edit reconfigures.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${FBS_SCHEMAS})

# settings.fbs includes lib_core's core_settings.fbs, whose generated header lives with lib_core.
z13_add_library(${PROJECT_NAME} INTERFACE PUBLIC zodiac13::core flatbuffers::flatbuffers)
add_dependencies(${PROJECT_NAME} FBS_SCHEMAS_HEADERS)

target_include_directories(
  ${PROJECT_NAME}
  INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/FBS_SCHEMAS_HEADERS>
  $<INSTALL_INTERFACE:${Z13_INSTALL_INCLUDE_DIR}>
)
