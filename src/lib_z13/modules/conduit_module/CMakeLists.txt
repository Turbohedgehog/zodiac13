project(conduit_module)

find_package(Eigen3 REQUIRED NO_MODULE)
find_package(Boost REQUIRED COMPONENTS dll)
find_package(flatbuffers CONFIG REQUIRED)

# The schema only this module uses; it includes the shared ones.
z13_generate_schemas(
  CONDUIT_MODULE_SCHEMAS
  SCHEMAS ${PROJECT_SOURCE_DIR}/fbs/conduit.fbs
  INCLUDE ${Z13_SHARED_SCHEMA_DIR}
)

z13_add_plugin(
  ${PROJECT_NAME} modules/conduit_module FBS_HEADERS EXPORT_ALL_SYMBOLS
  PRECOMPILE "<flecs.h>" "<Eigen/Dense>"
  SOURCES
  src/conduit_index.cpp
  src/conduit_kinds.cpp
  src/conduit_module.cpp
  src/conduit_module_factory.cpp
  src/conduit_run.cpp
  src/conduit_tool_system.cpp
  src/network_system.cpp
  PUBLIC
  Eigen3::Eigen
  PRIVATE
  zodiac13::core
  zodiac13::components
  zodiac13::z13_module
  zodiac13::settings
  zodiac13::primitives
  zodiac13::grid
  zodiac13::networks
  flatbuffers::flatbuffers
  Boost::dll
)

add_dependencies(${PROJECT_NAME} GENERATE_CONDUIT_MODULE_SCHEMAS)
target_include_directories(
  ${PROJECT_NAME} PRIVATE $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/CONDUIT_MODULE_SCHEMAS>)
