project(station_editor_module)

find_package(Eigen3 REQUIRED NO_MODULE)
find_package(Boost REQUIRED COMPONENTS dll)

z13_add_plugin(
  ${PROJECT_NAME} modules/station_editor_module EXPORT_ALL_SYMBOLS
  PRECOMPILE "<flecs.h>" "<Eigen/Dense>"
  SOURCES
  src/editor_commands.cpp
  src/editor_overlay_system.cpp
  src/scene_reload_system.cpp
  src/station_editor_module.cpp
  src/station_editor_module_factory.cpp
  PUBLIC
  Eigen3::Eigen
  PRIVATE
  zodiac13::core
  zodiac13::components
  zodiac13::player
  zodiac13::station_graph
  Boost::dll
)
