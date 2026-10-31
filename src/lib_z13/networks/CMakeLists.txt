project(networks)

find_package(Eigen3 REQUIRED NO_MODULE)

# Stateless, like rooms: each plugin linking its own copy shares nothing.
z13_add_library(
  ${PROJECT_NAME} STATIC OPTIMIZE_IN_DEBUG
  SOURCES
  src/networks.cpp
  PUBLIC
  Eigen3::Eigen
  zodiac13::components
  zodiac13::primitives
)
