cmake_minimum_required(VERSION 3.21)

project(zodiac13)

set(ZODIAC_EXECUTABLE_PROJECT_NAME ${PROJECT_NAME})

find_package(flecs CONFIG REQUIRED)

set(CMAKE_CXX_STANDARD 23)

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

# message("~~~~ CMAKE_CURRENT_SOURCE_DIR = ${CMAKE_CURRENT_SOURCE_DIR}")

# target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
# include_directories(${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(
  ${PROJECT_NAME}
  PRIVATE
  zodiac13::z13_launcher
  flecs::flecs_static
)

SET(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "${OUTPUT_DIRECTORY}")
SET(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${OUTPUT_DIRECTORY}")

set_target_properties(${PROJECT_NAME} PROPERTIES 
    RUNTIME_OUTPUT_DIRECTORY ${Z13_BIN_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${Z13_BIN_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${Z13_BIN_DIR}
    # MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)

# 
# set_target_properties(${PROJECT_NAME} PROPERTIES 
#     RUNTIME_OUTPUT_DIRECTORY ${ZODIAC_13_ROOT_DIR}/bin
#     RUNTIME_OUTPUT_DIRECTORY_DEBUG ${ZODIAC_13_ROOT_DIR}/bin
#     RUNTIME_OUTPUT_DIRECTORY_RELEASE ${ZODIAC_13_ROOT_DIR}/bin
#     # MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
# )

# Installed for the package by cmake/packaging.cmake.
