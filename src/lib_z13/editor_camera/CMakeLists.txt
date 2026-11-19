project(editor_camera)

find_package(Eigen3 REQUIRED NO_MODULE)

# Stateless, like rooms: each plugin linking its own copy shares nothing.
z13_add_library(
  ${PROJECT_NAME} STATIC
  SOURCES
  src/orbit_camera.cpp
  PUBLIC
  Eigen3::Eigen
  zodiac13::components
)
