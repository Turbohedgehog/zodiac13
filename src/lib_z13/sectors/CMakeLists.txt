project(sectors)

find_package(Eigen3 REQUIRED NO_MODULE)

# Stateless, like grid: each plugin linking its own copy shares nothing.
z13_add_library(
  ${PROJECT_NAME} STATIC FBS_HEADERS
  SOURCES
  src/interest_map.cpp
  src/sector.cpp
  src/sector_index.cpp
  PUBLIC
  Eigen3::Eigen
  zodiac13::fbs_schemas
  zodiac13::primitives
)
