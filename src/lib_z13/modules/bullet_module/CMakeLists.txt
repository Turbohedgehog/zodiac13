project(bullet_module)

find_package(Bullet REQUIRED)
find_package(Eigen3 REQUIRED NO_MODULE)
find_package(Boost REQUIRED COMPONENTS dll)

z13_add_plugin(
  ${PROJECT_NAME} modules/bullet_module EXPORT_ALL_SYMBOLS
  SOURCES
  src/block_shape.cpp
  src/bullet_module.cpp
  src/bullet_module_factory.cpp
  src/physics_system.cpp
  src/physics_world.cpp
  PUBLIC
  Eigen3::Eigen
  PRIVATE
  zodiac13::core
  zodiac13::components
  zodiac13::primitives
  zodiac13::settings
  ${BULLET_LIBRARIES}
  Boost::dll
)

target_include_directories(${PROJECT_NAME} PUBLIC $<BUILD_INTERFACE:${BULLET_INCLUDE_DIRS}>)
