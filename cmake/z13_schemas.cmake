find_package(flatbuffers CONFIG REQUIRED)

find_path(FLATBUFFERS_CMAKE_DIR BuildFlatBuffers.cmake
          HINTS ${flatbuffers_DIR} "${flatbuffers_DIR}/../../../share/flatbuffers")
include(${FLATBUFFERS_CMAKE_DIR}/BuildFlatBuffers.cmake)

set(Z13_FLATC_FLAGS
  "--bfbs-gen-embed"
  "--scoped-enums"
  "--reflect-names"
  "--gen-name-strings"
  "--gen-object-api"
  "--gen-compare"
  "--cpp-std" "c++17"
)

# Appends to `out_var` the schemas `schema` includes, directly or through others, looked for in
# the schema's own directory and in `dirs`.
function(z13_fbs_includes schema out_var dirs)
  set(found ${${out_var}})
  get_filename_component(own_dir ${schema} DIRECTORY)
  file(STRINGS ${schema} include_lines REGEX "^include \"[^\"]+\"")
  foreach(line IN LISTS include_lines)
    string(REGEX REPLACE "^include \"([^\"]+)\".*" "\\1" included "${line}")
    foreach(dir IN LISTS own_dir dirs)
      if(EXISTS ${dir}/${included} AND NOT ${dir}/${included} IN_LIST found)
        list(APPEND found ${dir}/${included})
        z13_fbs_includes(${dir}/${included} found "${dirs}")
        break()
      endif()
    endforeach()
  endforeach()
  set(${out_var} ${found} PARENT_SCOPE)
endfunction()

# z13_generate_schemas(<target> SCHEMAS <file.fbs>... [INCLUDE <dir>...])
# Generates <name>_generated.h for each schema into ${CMAKE_CURRENT_BINARY_DIR}/<target>, which
# the caller adds to its include directories; INCLUDE lists where the schemas' includes are.
# flatc's rules depend only on their own schema, but --bfbs-gen-embed embeds what a schema
# includes too: each output also depends on the schemas its schema includes, and no others, so
# a schema nobody includes regenerates its own header only.
function(z13_generate_schemas target)
  cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "SCHEMAS;INCLUDE")
  if(arg_UNPARSED_ARGUMENTS OR NOT arg_SCHEMAS)
    message(FATAL_ERROR "z13_generate_schemas(${target}): SCHEMAS are required, no other arguments known")
  endif()

  flatbuffers_generate_headers(
    TARGET ${target}
    SCHEMAS ${arg_SCHEMAS}
    BINARY_SCHEMAS_DIR "${CMAKE_CURRENT_BINARY_DIR}/${target}_bfbs"
    INCLUDE ${arg_INCLUDE}
    FLAGS ${Z13_FLATC_FLAGS}
  )

  foreach(schema IN LISTS arg_SCHEMAS)
    get_filename_component(schema_name ${schema} NAME_WE)
    set(included "")
    z13_fbs_includes(${schema} included "${arg_INCLUDE}")
    if(included)
      add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${target}/${schema_name}_generated.h
        APPEND DEPENDS ${included})
      add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${target}_bfbs/${schema_name}.bfbs
        APPEND DEPENDS ${included})
    endif()
  endforeach()
  # The include lines are read here, so a schema edit reconfigures.
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${arg_SCHEMAS})
endfunction()
