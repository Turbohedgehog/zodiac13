project(outline)

find_package(Eigen3 REQUIRED NO_MODULE)

# Stateless, like rooms: the outlines of the rooms the overlay draws, heavy on Eigen and hash sets.
z13_add_library(
  ${PROJECT_NAME} STATIC OPTIMIZE_IN_DEBUG
  SOURCES
  src/outline_edges.cpp
  src/room_boundary.cpp
  src/room_box_cache.cpp
  src/room_outline.cpp
  src/span_vertices.cpp
  src/wall_faces.cpp
  PUBLIC
  Eigen3::Eigen
  zodiac13::components
  zodiac13::grid
  zodiac13::primitives
  zodiac13::rooms
)
