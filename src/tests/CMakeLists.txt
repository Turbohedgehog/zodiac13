cmake_minimum_required(VERSION 3.21)

project(z13_tests)

set(CMAKE_CXX_STANDARD 23)

find_package(GTest CONFIG REQUIRED)
find_package(reflectcpp CONFIG REQUIRED)

include(GenerateExportHeader)
include(GoogleTest)

file(
  GLOB_RECURSE
  PROJECT_SOURCE_FILES
  ${PROJECT_SOURCE_DIR}/src/*.h
  ${PROJECT_SOURCE_DIR}/src/*.cpp
)

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
  GTest::gtest
  GTest::gmock
  reflectcpp::reflectcpp
)

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
