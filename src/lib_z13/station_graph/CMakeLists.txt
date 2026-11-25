project(station_graph)

find_package(Eigen3 REQUIRED NO_MODULE)

# The properties' schema is a FlatBuffers one, read through reflection (property_schema.h).
z13_generate_schemas(
  STATION_PROPERTIES_SCHEMAS
  SCHEMAS
  ${PROJECT_SOURCE_DIR}/fbs/station_properties.fbs
  INCLUDE ${CMAKE_SOURCE_DIR}/src/lib_core/fbs
)

# Stateless, like rooms: each plugin linking its own copy shares nothing.
z13_add_library(
  ${PROJECT_NAME} STATIC
  SOURCES
  src/graph.cpp
  src/graph_assets.cpp
  src/graph_diagnosis.cpp
  src/graph_editor.cpp
  src/graph_geometry.cpp
  src/graph_json.cpp
  src/graph_drag.cpp
  src/graph_pick.cpp
  src/property_schema.cpp
  PUBLIC
  Eigen3::Eigen
  zodiac13::core
  PRIVATE
  zodiac13::fbs_schemas
)

add_dependencies(${PROJECT_NAME} GENERATE_STATION_PROPERTIES_SCHEMAS)
target_include_directories(${PROJECT_NAME} PRIVATE $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/STATION_PROPERTIES_SCHEMAS>)
