cmake_minimum_required(VERSION 3.13)

project(core)

set(CMAKE_CXX_STANDARD 20)

# boost
set (BOOST_ROOT ${PROJECT_SOURCE_DIR}/../../../boost)
set (BOOST_INCLUDE_DIR ${BOOST_ROOT})
set (Boost_NO_SYSTEM_PATHS ON)
set (Boost_USE_MULTITHREADED ON)
set (Boost_USE_STATIC_LIBS ON)
set (Boost_USE_STATIC_RUNTIME OFF)
set (BOOST_ALL_DYN_LINK OFF)

find_package(Boost REQUIRED COMPONENTS program_options)

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
  ${Boost_INCLUDE_DIR}
)

# target_link_libraries(${PROJECT_NAME} PRIVATE Boost::program_options)
target_link_libraries(
  ${PROJECT_NAME}
  PRIVATE
  flecs::flecs_static
  Boost::program_options
)

set(Z13_CORE_INCLUDE_DIR ${PROJECT_SOURCE_DIR} CACHE PATH "core lib include dirs" FORCE)
set(Z13_CORE_LIB ${PROJECT_NAME} CACHE PATH "core lib" FORCE)
