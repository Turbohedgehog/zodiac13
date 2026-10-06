cmake_minimum_required(VERSION 3.21)

project(z13_tests)

set(CMAKE_CXX_STANDARD 23)

find_package(GTest CONFIG REQUIRED)
find_package(reflectcpp CONFIG REQUIRED)

include(GenerateExportHeader)
include(GoogleTest)

# Test code lives either here (for cross-module/serialization tests) or next to
# the module it covers, under that module's own tests/ directory.
file(
  GLOB_RECURSE
  PROJECT_SOURCE_FILES
  ${PROJECT_SOURCE_DIR}/src/*.h
  ${PROJECT_SOURCE_DIR}/src/*.cpp
  ${CMAKE_SOURCE_DIR}/src/lib_z13/modules/*/tests/*.h
  ${CMAKE_SOURCE_DIR}/src/lib_z13/modules/*/tests/*.cpp
)

if(NOT Z13_BUILD_BENCHMARKS)
  list(FILTER PROJECT_SOURCE_FILES EXCLUDE REGEX "/tests/bench/")
endif()

# Standalone DLL exercising lib_core and lib_z13.
add_library(${PROJECT_NAME} SHARED ${PROJECT_SOURCE_FILES})
add_library(${PROJECT_NAMESPACE}::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

# Third-party test headers (reflect-cpp, gtest, flecs meta) are noisy under /W4.
set_target_properties(${PROJECT_NAME} PROPERTIES COMPILE_WARNING_AS_ERROR OFF)

generate_export_header(${PROJECT_NAME}
  BASE_NAME Z13_TESTS
  EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/z13_tests/z13_tests_export.h
)

target_include_directories(
  ${PROJECT_NAME}
  PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
)

target_link_libraries(
  ${PROJECT_NAME}
  PRIVATE
  zodiac13::core
  zodiac13::components
  zodiac13::z13_module
  zodiac13::building_module
  zodiac13::bullet_module
  zodiac13::station_module
  zodiac13::net_module
  zodiac13::z13_launcher
  zodiac13::z13_settings
  zodiac13::z13_primitives
  zodiac13::z13_grid
  GTest::gtest
  GTest::gmock
  reflectcpp::reflectcpp
)

# The palette tests read the shipped palette from the source tree.
target_compile_definitions(${PROJECT_NAME} PRIVATE Z13_SOURCE_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets")

if(Z13_BUILD_BENCHMARKS)
  # StationLoadBench reads the station the layout generator wrote.
  target_compile_definitions(${PROJECT_NAME} PRIVATE Z13_STATION_LAYOUT_DIR="${CMAKE_SOURCE_DIR}/docs/station-layout")
endif()

# Windows finds the plugin DLLs only next to the exe; ELF finds them by build-tree RPATH.
if(WIN32)
  add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      $<TARGET_FILE:zodiac13::core>
      $<TARGET_FILE:zodiac13::z13_module>
      $<TARGET_FILE:zodiac13::building_module>
      $<TARGET_FILE:zodiac13::bullet_module>
      $<TARGET_FILE:zodiac13::station_module>
      $<TARGET_FILE:zodiac13::net_module>
      $<TARGET_FILE_DIR:${PROJECT_NAME}>
  )
endif()

SET(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "${OUTPUT_DIRECTORY}")
SET(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${OUTPUT_DIRECTORY}")

set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/tests
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/bin/tests
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/bin/tests
)

# Host executable CTest invokes; the test code stays in the DLL.
add_executable(z13_test_runner runner/main.cpp)
target_link_libraries(z13_test_runner PRIVATE ${PROJECT_NAME} GTest::gtest)
set_target_properties(z13_test_runner PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/tests
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/bin/tests
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/bin/tests
)

gtest_discover_tests(z13_test_runner
    DISCOVERY_MODE PRE_TEST
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/tests
)

# The launcher's plugin path, out of process: z13_test_runner links the plugins directly
# instead, so it can't tell whether separately loaded plugins share one flecs.
find_package(Boost REQUIRED COMPONENTS dll)
add_executable(z13_plugin_smoke plugin_smoke/main.cpp)
target_link_libraries(z13_plugin_smoke PRIVATE zodiac13::core zodiac13::z13_launcher Boost::dll)
# Only for test_dll_messages.h; the plugin itself is loaded, not linked.
target_include_directories(z13_plugin_smoke PRIVATE ${CMAKE_SOURCE_DIR}/src/lib_test_dll/include)
set_target_properties(z13_plugin_smoke PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/tests
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/bin/tests
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/bin/tests
)
if(WIN32)
  add_custom_command(TARGET z13_plugin_smoke POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:zodiac13::core> $<TARGET_FILE_DIR:z13_plugin_smoke>
  )
endif()
add_dependencies(z13_plugin_smoke z13_config z13_module building_module net_module bullet_module station_module test_dll_module)
add_test(NAME PluginSmoke COMMAND z13_plugin_smoke)
