project(cell_light)

find_package(Eigen3 REQUIRED NO_MODULE)

# Stateless, like grid: each plugin linking its own copy shares nothing.
z13_add_library(
  ${PROJECT_NAME} STATIC FBS_HEADERS
  PRECOMPILE "<Eigen/Dense>"
  SOURCES
  src/cell_light.cpp
  src/cell_light_state.cpp
  src/lamp.cpp
  src/lamp_flood.cpp
  src/light_nodes.cpp
  PUBLIC
  Eigen3::Eigen
  zodiac13::fbs_schemas
  zodiac13::primitives
)
