project(building_module)

find_package(Eigen3 REQUIRED NO_MODULE)
find_package(Boost REQUIRED COMPONENTS dll)

z13_add_plugin(
  ${PROJECT_NAME} modules/building_module FBS_HEADERS EXPORT_ALL_SYMBOLS
  SOURCES
  src/block_building_system.cpp
  src/block_cutting.cpp
  src/block_entities.cpp
  src/block_snapshot_system.cpp
  src/block_watch.cpp
  src/brush_aim.cpp
  src/brush_system.cpp
  src/build_action_ids.cpp
  src/build_validation.cpp
  src/building_input_system.cpp
  src/building_module.cpp
  src/building_module_factory.cpp
  src/building_system.cpp
  src/palette_system.cpp
  src/station_scene_system.cpp
  PUBLIC
  Eigen3::Eigen
  PRIVATE
  zodiac13::core
  zodiac13::components
  zodiac13::z13_module
  zodiac13::settings
  zodiac13::primitives
  zodiac13::grid
  Boost::dll
)
