project(grid)

find_package(Eigen3 REQUIRED NO_MODULE)

# Stateless, like primitives: each plugin linking its own copy shares nothing.
z13_add_library(
  ${PROJECT_NAME} STATIC FBS_HEADERS
  SOURCES
  src/block_index.cpp
  PUBLIC
  Eigen3::Eigen
  zodiac13::fbs_schemas
  zodiac13::primitives
)
