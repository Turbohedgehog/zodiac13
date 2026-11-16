project(player)

find_package(Eigen3 REQUIRED NO_MODULE)

# roles.json is read by FlatBuffers' parser: reflect-cpp here would put a copy of its globals in every plugin.
z13_generate_schemas(
  PLAYER_SCHEMAS
  SCHEMAS
  ${PROJECT_SOURCE_DIR}/fbs/roles.fbs
  INCLUDE ${Z13_SHARED_SCHEMA_DIR}
)

# Stateless, like touch: spawning a player and the camera's look are shared by z13_module,
# building_module and net_module, each linking its own copy.
z13_add_library(
  ${PROJECT_NAME} STATIC FBS_HEADERS
  SOURCES
  src/camera_look.cpp
  src/gameplay_entities.cpp
  src/roles.cpp
  PUBLIC
  Eigen3::Eigen
  zodiac13::components
  zodiac13::core
  PRIVATE
  zodiac13::fbs_schemas
  zodiac13::settings
  zodiac13::input
)

add_dependencies(${PROJECT_NAME} GENERATE_PLAYER_SCHEMAS)
target_include_directories(${PROJECT_NAME} PRIVATE $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/PLAYER_SCHEMAS>)
