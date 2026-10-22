project(rooms)

find_package(Eigen3 REQUIRED NO_MODULE)
find_package(Boost REQUIRED COMPONENTS geometry graph)

# Stateless, like grid: each plugin linking its own copy shares nothing.
z13_add_library(
  ${PROJECT_NAME} STATIC FBS_HEADERS
  PRECOMPILE "<Eigen/Dense>"
  SOURCES
  src/cell_boxes.cpp
  src/free_space.cpp
  src/local_leaves.cpp
  src/numbered_leaves.cpp
  src/openings.cpp
  src/parallel_for.cpp
  src/portal_source.cpp
  src/room_sources.cpp
  src/room_builder.cpp
  src/room_cache.cpp
  src/room_changes.cpp
  src/room_graph.cpp
  src/room_overlap.cpp
  src/room_parts.cpp
  src/room_update.cpp
  src/room_visibility.cpp
  src/topology_fingerprint.cpp
  src/tour_viewpoints.cpp
  PUBLIC
  Eigen3::Eigen
  Boost::geometry
  Boost::graph
  zodiac13::fbs_schemas
  zodiac13::primitives
  zodiac13::grid
  zodiac13::z13_profile
)
