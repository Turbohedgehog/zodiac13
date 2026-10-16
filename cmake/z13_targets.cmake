# Target helpers for the CMakeLists.txt under src/: one place for the include layout, the output
# directories and the install rules. Include before the add_subdirectory() calls.

set(Z13_INSTALL_INCLUDE_DIR include/${PROJECT_NAMESPACE}-${Z13_FULL_VERSION})
set(Z13_INSTALL_CMAKE_DIR lib/cmake/${PROJECT_NAMESPACE}-${Z13_FULL_VERSION})

# Puts a target's binaries into `directory` itself: multi-config generators otherwise add a
# per-configuration subdirectory. LIBRARY places a .so, RUNTIME a .dll or an executable.
function(z13_set_output_directory target directory)
  foreach(config IN ITEMS "" _DEBUG _RELEASE)
    set_target_properties(${target} PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY${config} ${directory}
      LIBRARY_OUTPUT_DIRECTORY${config} ${directory}
    )
  endforeach()
endfunction()

# z13_add_library(<name> <STATIC|SHARED|INTERFACE> [FBS_HEADERS] [EXPORT_ALL_SYMBOLS]
#                 [PRECOMPILE ...] [SOURCES ...] [PUBLIC ...] [PRIVATE ...])
# Adds zodiac13::<name> with the directory's include/ as its public headers, and installs it.
# PRECOMPILE lists the heavy headers most of its sources include (Z13_PRECOMPILE_HEADERS).
function(z13_add_library name type)
  cmake_parse_arguments(PARSE_ARGV 2 arg "FBS_HEADERS;EXPORT_ALL_SYMBOLS" "" "PRECOMPILE;SOURCES;PUBLIC;PRIVATE")
  if(arg_UNPARSED_ARGUMENTS)
    message(FATAL_ERROR "z13_add_library(${name}): unknown arguments ${arg_UNPARSED_ARGUMENTS}")
  endif()

  add_library(${name} ${type} ${arg_SOURCES})
  add_library(${PROJECT_NAMESPACE}::${name} ALIAS ${name})

  set(public PUBLIC)
  if(type STREQUAL "INTERFACE")
    set(public INTERFACE)
  endif()
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_include_directories(${name} ${public}
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
      $<INSTALL_INTERFACE:${Z13_INSTALL_INCLUDE_DIR}>
    )
    install(DIRECTORY include/ DESTINATION ${Z13_INSTALL_INCLUDE_DIR}/)
  endif()
  if(arg_PUBLIC)
    target_link_libraries(${name} ${public} ${arg_PUBLIC})
  endif()
  if(arg_PRIVATE)
    target_link_libraries(${name} PRIVATE ${arg_PRIVATE})
  endif()

  # Depend on the concrete flatc target, not the fbs_schemas INTERFACE library: under the VS
  # generator that makes no build-order edge, and a fresh build misses the generated headers.
  if(arg_FBS_HEADERS)
    add_dependencies(${name} GENERATE_FBS_SCHEMAS_HEADERS)
  endif()
  if(arg_PRECOMPILE AND Z13_PRECOMPILE_HEADERS)
    target_precompile_headers(${name} PRIVATE ${arg_PRECOMPILE})
  endif()
  # For a library whose users call its internal symbols: MSVC exports nothing by default.
  if(arg_EXPORT_ALL_SYMBOLS)
    set_target_properties(${name} PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
  endif()

  install(TARGETS ${name}
    EXPORT ${name}Targets
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
  )
  install(EXPORT ${name}Targets
    FILE ${PROJECT_NAMESPACE}_${name}Targets.cmake
    NAMESPACE ${PROJECT_NAMESPACE}::
    DESTINATION ${Z13_INSTALL_CMAKE_DIR}/
  )
endfunction()

# z13_add_plugin(<name> <output subdirectory of Z13_BIN_DIR> <z13_add_library options>...)
# A shared library the launcher loads by the path in z13_config.yaml, which has no "lib" prefix.
function(z13_add_plugin name output_subdirectory)
  z13_add_library(${name} SHARED ${ARGN})
  set_target_properties(${name} PROPERTIES PREFIX "")
  z13_set_output_directory(${name} ${Z13_BIN_DIR}/${output_subdirectory})
endfunction()
