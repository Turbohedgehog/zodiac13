project(settings)

find_package(platform_folders REQUIRED)

z13_add_library(
  ${PROJECT_NAME} STATIC FBS_HEADERS
  SOURCES
  src/environment.cpp
  src/settings.cpp
  src/settings_file.cpp
  PUBLIC
  zodiac13::core
  zodiac13::fbs_schemas
  PRIVATE
  zodiac13::components
  sago::platform_folders
)
