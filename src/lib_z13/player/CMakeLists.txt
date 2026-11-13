project(player)

find_package(Eigen3 REQUIRED NO_MODULE)
find_package(reflectcpp CONFIG REQUIRED)

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
  reflectcpp::reflectcpp
  zodiac13::settings
  zodiac13::input
)
