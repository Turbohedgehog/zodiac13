project(primitives)

find_package(Eigen3 REQUIRED NO_MODULE)

# The schemas only this library uses; they include the shared ones.
z13_generate_schemas(
  PRIMITIVES_SCHEMAS
  SCHEMAS
  ${PROJECT_SOURCE_DIR}/fbs/blueprint.fbs
  ${PROJECT_SOURCE_DIR}/fbs/decor.fbs
  INCLUDE ${Z13_SHARED_SCHEMA_DIR}
)

# Stateless functions only, so each plugin linking its own copy shares nothing.
z13_add_library(
  ${PROJECT_NAME} STATIC FBS_HEADERS
  PRECOMPILE "<Eigen/Dense>"
  SOURCES
  src/blueprint.cpp
  src/chunk_mesh.cpp
  src/cut.cpp
  src/decor.cpp
  src/draw_order.cpp
  src/geometry.cpp
  src/lights.cpp
  src/occupancy.cpp
  src/palette.cpp
  src/panel_prism.cpp
  src/placement.cpp
  src/station_assets.cpp
  src/triangle_panel.cpp
  src/wall_bend.cpp
  src/wall_opening.cpp
  src/wall_panel.cpp
  src/wall_seam.cpp
  PUBLIC
  Eigen3::Eigen
  zodiac13::fbs_schemas
  zodiac13::components
)

add_dependencies(${PROJECT_NAME} GENERATE_PRIMITIVES_SCHEMAS)
target_include_directories(${PROJECT_NAME} PRIVATE $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/PRIMITIVES_SCHEMAS>)
