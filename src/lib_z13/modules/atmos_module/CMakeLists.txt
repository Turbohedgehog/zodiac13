project(atmos_module)

find_package(Eigen3 REQUIRED NO_MODULE)
find_package(Boost REQUIRED COMPONENTS dll)

z13_add_plugin(
  ${PROJECT_NAME} modules/atmos_module EXPORT_ALL_SYMBOLS
  PRECOMPILE "<flecs.h>" "<Eigen/Dense>"
  SOURCES
  src/airlock.cpp
  src/atmos_module.cpp
  src/atmos_module_factory.cpp
  src/atmos_overlay_system.cpp
  src/atmos_system.cpp
  src/door_at.cpp
  src/room_air_carry.cpp
  src/room_air_store.cpp
  src/room_index.cpp
  PUBLIC
  Eigen3::Eigen
  PRIVATE
  zodiac13::core
  zodiac13::components
  zodiac13::settings
  zodiac13::primitives
  zodiac13::networks
  zodiac13::rooms
  zodiac13::atmos
  Boost::dll
)
