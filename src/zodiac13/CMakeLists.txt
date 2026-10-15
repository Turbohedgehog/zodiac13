project(zodiac13)

find_package(flecs CONFIG REQUIRED)

if(ANDROID)
  find_package(SDL3 CONFIG REQUIRED)
  find_package(spdlog CONFIG REQUIRED)
  add_library(${PROJECT_NAME} SHARED android_main.cpp)
  set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME main)
  target_link_libraries(
    ${PROJECT_NAME}
    PRIVATE
    zodiac13::z13_launcher
    zodiac13::core
    SDL3::SDL3
    spdlog::spdlog
    log
  )
  return()
endif()

add_executable(${PROJECT_NAME} main.cpp)
target_link_libraries(
  ${PROJECT_NAME}
  PRIVATE
  zodiac13::z13_launcher
  flecs::flecs_static
)
z13_set_output_directory(${PROJECT_NAME} ${Z13_BIN_DIR})

# Installed for the package by cmake/packaging.cmake.
