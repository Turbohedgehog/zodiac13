cmake_minimum_required(VERSION 3.21)

project(rooms)

set(CMAKE_CXX_STANDARD 23)

find_package(Eigen3 REQUIRED NO_MODULE)

set(
  PROJECT_SOURCE_FILES
  src/column_map.cpp
  src/disjoint_sets.cpp
  src/portal_source.cpp
  src/room_builder.cpp
  src/room_cache.cpp
  src/room_overlap.cpp
  src/room_visibility.cpp
  src/topology_fingerprint.cpp
)

# Stateless, like z13_grid: each plugin linking its own copy shares nothing.
add_library(${PROJECT_NAME} STATIC ${PROJECT_SOURCE_FILES})
add_library(${PROJECT_NAMESPACE}::${PROJECT_NAME} ALIAS ${PROJECT_NAME})
set_target_properties(${PROJECT_NAME} PROPERTIES POSITION_INDEPENDENT_CODE ON)

# primitives_generated.h (via palette.h) comes from the concrete flatc target.
add_dependencies(${PROJECT_NAME} GENERATE_FBS_SCHEMAS_HEADERS)

target_include_directories(
  ${PROJECT_NAME}
  PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include/${PROJECT_NAMESPACE}-${PROJECT_VERSION}>
)

target_link_libraries(
  ${PROJECT_NAME}
  PUBLIC
  Eigen3::Eigen
  zodiac13::fbs_schemas
  zodiac13::primitives
)

install(DIRECTORY include/ DESTINATION include/${PROJECT_NAMESPACE}-${PROJECT_VERSION}/)

install(TARGETS ${PROJECT_NAME}
        EXPORT ${PROJECT_NAME}Targets
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
)

install(EXPORT ${PROJECT_NAME}Targets
        FILE ${PROJECT_NAMESPACE}_${PROJECT_NAME}Targets.cmake
        NAMESPACE ${PROJECT_NAMESPACE}::
        DESTINATION lib/cmake/${PROJECT_NAMESPACE}-${PROJECT_VERSION}/
)
