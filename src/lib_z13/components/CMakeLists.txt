project(components)

find_package(Boost REQUIRED COMPONENTS container)
find_package(flecs CONFIG REQUIRED)
find_package(Eigen3 REQUIRED NO_MODULE)

z13_add_library(
  ${PROJECT_NAME} INTERFACE
  PUBLIC
  zodiac13::fbs_schemas
  Boost::container
  Eigen3::Eigen
  flecs::flecs_static
)
