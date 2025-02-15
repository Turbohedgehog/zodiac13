cmake_minimum_required(VERSION 3.13)

project(core)

set(CMAKE_CXX_STANDARD 20)

option(FLECS_SHARED "Build shared flecs lib" OFF)
add_subdirectory(${PROJECT_SOURCE_DIR}/../../flecs ${CMAKE_CURRENT_BINARY_DIR}/flecs)

file(
  GLOB_RECURSE
  PROJECT_HEADER_FILES
  ${PROJECT_SOURCE_DIR}/*.h
)

file(
  GLOB_RECURSE
  PROJECT_SOURCE_FILES
  ${PROJECT_SOURCE_DIR}/*.cpp
)

add_library(${PROJECT_NAME} STATIC ${PROJECT_SOURCE_FILES})

target_include_directories(
  ${PROJECT_NAME}
  PUBLIC
  ${PROJECT_HEADER_FILES}
  ${ZODIAC_13_ROOT_DIR}/flecs/include
)

target_link_libraries(${PROJECT_NAME} flecs::flecs_static)

set(Z13_CORE_INCLUDE_DIR ${PROJECT_SOURCE_DIR} CACHE PATH "core lib include dirs" FORCE)
set(Z13_CORE_LIB ${PROJECT_NAME} CACHE PATH "core lib" FORCE)

