project(station_graph)

find_package(Eigen3 REQUIRED NO_MODULE)

# Stateless, like rooms: each plugin linking its own copy shares nothing.
z13_add_library(
  ${PROJECT_NAME} STATIC
  SOURCES
  src/graph.cpp
  src/graph_assets.cpp
  src/graph_diagnosis.cpp
  src/graph_editor.cpp
  src/graph_json.cpp
  src/property_schema.cpp
  PUBLIC
  Eigen3::Eigen
  zodiac13::core
)
