project(station_module)

find_package(Eigen3 REQUIRED NO_MODULE)
find_package(Boost REQUIRED COMPONENTS dll)

z13_add_plugin(
  ${PROJECT_NAME} modules/station_module EXPORT_ALL_SYMBOLS
  PRECOMPILE "<flecs.h>" "<Eigen/Dense>"
  SOURCES
  src/door_system.cpp
  src/gravity_system.cpp
  src/interaction_system.cpp
  src/outline_edges.cpp
  src/room_boundary.cpp
  src/room_box_cache.cpp
  src/room_outline.cpp
  src/room_overlay_system.cpp
  src/room_system.cpp
  src/span_vertices.cpp
  src/station_module.cpp
  src/station_module_factory.cpp
  src/switch_system.cpp
  src/topology_sources.cpp
  src/wall_faces.cpp
  PUBLIC
  Eigen3::Eigen
  PRIVATE
  zodiac13::core
  zodiac13::components
  zodiac13::primitives
  zodiac13::rooms
  zodiac13::settings
  zodiac13::grid
  zodiac13::z13_module
  Boost::dll
)
