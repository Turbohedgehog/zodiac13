project(power_module)

find_package(Eigen3 REQUIRED NO_MODULE)
find_package(Boost REQUIRED COMPONENTS dll)

z13_add_plugin(
  ${PROJECT_NAME} modules/power_module EXPORT_ALL_SYMBOLS
  PRECOMPILE "<flecs.h>" "<Eigen/Dense>"
  SOURCES
  src/power_module.cpp
  src/power_module_factory.cpp
  src/power_overlay_system.cpp
  src/power_system.cpp
  PUBLIC
  Eigen3::Eigen
  PRIVATE
  zodiac13::core
  zodiac13::components
  zodiac13::z13_module
  zodiac13::settings
  zodiac13::primitives
  zodiac13::networks
  zodiac13::power
  Boost::dll
)
