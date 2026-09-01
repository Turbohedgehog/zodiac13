cmake_minimum_required(VERSION 3.28)

project(core CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(spdlog CONFIG REQUIRED)
find_package(Boost REQUIRED COMPONENTS program_options dll)
find_package(flecs CONFIG REQUIRED)

add_library(${PROJECT_NAME} STATIC)
add_library(${PROJECT_NAMESPACE}::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

target_sources(${PROJECT_NAME}
  PRIVATE
    src/core.cpp
    src/config.cpp
    src/module_lib_holder.cpp
    src/flecs_utils.cpp
  PUBLIC
    FILE_SET core_modules TYPE CXX_MODULES
    BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/src
    FILES
      src/core_types.cppm
      src/components.cppm
      src/config.cppm
      src/module_lib_holder.cppm
      src/core.cppm
)

target_include_directories(
  ${PROJECT_NAME}
  PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include/${PROJECT_NAMESPACE}-${PROJECT_VERSION}>
  $<BUILD_INTERFACE:${Boost_INCLUDE_DIR}>
)

target_link_libraries(
  ${PROJECT_NAME}
  PRIVATE
  Boost::program_options
  Boost::dll
)

target_link_libraries(
  ${PROJECT_NAME}
  PUBLIC
  flecs::flecs_static
  spdlog::spdlog_header_only
)

install(DIRECTORY include/ DESTINATION include/${PROJECT_NAMESPACE}-${PROJECT_VERSION}/)

# NOTE: install(TARGETS/EXPORT) for module-providing targets is not supported by
# the Visual Studio generator (no BMI/module-info install). This tree consumes
# every target in-tree via add_subdirectory + ALIAS, and packaging ships bin/
# through CPack, so the export rules are dropped rather than worked around.
