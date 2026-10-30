project(conduit_render_module)

find_package(Eigen3 REQUIRED NO_MODULE)
find_package(Boost REQUIRED COMPONENTS dll)

z13_add_plugin(
  ${PROJECT_NAME} modules/conduit_render_module EXPORT_ALL_SYMBOLS
  PRECOMPILE "<flecs.h>" "<Eigen/Dense>"
  SOURCES
  src/conduit_mesh.cpp
  src/conduit_mesh_system.cpp
  src/conduit_render_module.cpp
  src/conduit_render_module_factory.cpp
  PUBLIC
  Eigen3::Eigen
  PRIVATE
  zodiac13::core
  zodiac13::components
  Boost::dll
)
