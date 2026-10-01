cmake_minimum_required(VERSION 3.21)

project(fbs_schemas)

set(CMAKE_CXX_STANDARD 23)

find_package(flatbuffers CONFIG REQUIRED)

find_path(FLATBUFFERS_CMAKE_DIR BuildFlatBuffers.cmake 
          HINTS ${flatbuffers_DIR} "${flatbuffers_DIR}/../../../share/flatbuffers")
include(${FLATBUFFERS_CMAKE_DIR}/BuildFlatBuffers.cmake)

set(FBS_DIR ${PROJECT_SOURCE_DIR}/fbs/)

file(
    GLOB_RECURSE
    FBS_SCHEMAS
    ${FBS_DIR}/*.fbs
)

add_library(${PROJECT_NAME} INTERFACE)
add_library(${PROJECT_NAMESPACE}::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

set(
    GEN_FLAT_FLAGS
    "--bfbs-gen-embed"
    "--scoped-enums"
    "--reflect-names"
    "--gen-name-strings"
    "--gen-object-api"
    "--gen-compare"
    "--cpp-std" "c++17"
    # "--cpp-str-type" "std::string_view"
)

flatbuffers_generate_headers(
    TARGET FBS_SCHEMAS_HEADERS
    SCHEMAS ${FBS_SCHEMAS}
    BINARY_SCHEMAS_DIR "${CMAKE_CURRENT_BINARY_DIR}/bfbs"
    INCLUDE_DIR ${FBS_DIR}
    INCLUDE ${CMAKE_SOURCE_DIR}/src/lib_core/fbs
    FLAGS ${GEN_FLAT_FLAGS}
)

add_dependencies(${PROJECT_NAME} FBS_SCHEMAS_HEADERS)

set(GEN_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/FBS_SCHEMAS_HEADERS")

target_include_directories(
  ${PROJECT_NAME}
  INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/FBS_SCHEMAS_HEADERS>
  $<INSTALL_INTERFACE:include/${PROJECT_NAMESPACE}-${PROJECT_VERSION}>
)

# settings.fbs includes lib_core's core_settings.fbs, whose generated header lives with lib_core.
target_link_libraries(${PROJECT_NAME} INTERFACE zodiac13::core flatbuffers::flatbuffers)

# install(
#     DIRECTORY include/
#     DESTINATION include/${PROJECT_NAMESPACE}-${PROJECT_VERSION}/
# )

install(
    TARGETS ${PROJECT_NAME}
    EXPORT ${PROJECT_NAME}Targets
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

install(
    EXPORT ${PROJECT_NAME}Targets
    FILE ${PROJECT_NAMESPACE}_${PROJECT_NAME}Targets.cmake
    NAMESPACE ${PROJECT_NAMESPACE}::
    DESTINATION lib/cmake/${PROJECT_NAMESPACE}-${PROJECT_VERSION}/
)