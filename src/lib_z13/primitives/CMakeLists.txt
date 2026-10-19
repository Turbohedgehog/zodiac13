project(primitives)

find_package(Eigen3 REQUIRED NO_MODULE)

# Stateless functions only, so each plugin linking its own copy shares nothing.
z13_add_library(
  ${PROJECT_NAME} STATIC FBS_HEADERS
  PRECOMPILE "<Eigen/Dense>"
  SOURCES
  src/blueprint.cpp
  src/chunk_mesh.cpp
  src/cut.cpp
  src/draw_order.cpp
  src/geometry.cpp
  src/lights.cpp
  src/occupancy.cpp
  src/palette.cpp
  src/placement.cpp
  src/station_assets.cpp
  src/wall_panel.cpp
  src/wall_seam.cpp
  PUBLIC
  Eigen3::Eigen
  zodiac13::fbs_schemas
  zodiac13::components
)
