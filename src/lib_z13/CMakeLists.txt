cmake_minimum_required(VERSION 3.13)

project(z13)

set(CMAKE_CXX_STANDARD 20)

# find_package(z13::core REQUIRED)

# set(CMAKE_INSTALL_PREFIX "${CMAKE_SOURCE_DIR}/install" CACHE PATH "Installation directory" FORCE)

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
  ${Z13_CORE_INCLUDE_DIR}
)

target_link_libraries(${PROJECT_NAME} PRIVATE ${Z13_CORE_LIB})

include_directories(
  ${PROJECT_SOURCE_DIR}
  ${ZODIAC_13_ROOT_DIR}/flecs/include
)

set(Z13_INCLUDE_DIR ${PROJECT_SOURCE_DIR} CACHE PATH "z13 lib include dirs" FORCE)
set(Z13_LIB ${PROJECT_NAME} CACHE PATH "z13 lib" FORCE)

