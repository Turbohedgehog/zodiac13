project(z13_launcher)

find_package(Boost REQUIRED COMPONENTS dll)
find_package(yaml-cpp CONFIG REQUIRED)

z13_add_library(
  ${PROJECT_NAME} STATIC FBS_HEADERS
  SOURCES
  src/module_list.cpp
  src/settings_loader.cpp
  src/station_assets_check.cpp
  src/z13_launcher.cpp
  PUBLIC
  zodiac13::settings
  PRIVATE
  zodiac13::core
  zodiac13::fbs_schemas
  zodiac13::primitives
  Boost::dll
  yaml-cpp::yaml-cpp
)
