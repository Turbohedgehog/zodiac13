# C++20 modules (FILE_SET CXX_MODULES) require CMake >= 3.28
cmake_minimum_required(VERSION 3.28)

project(core)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(spdlog CONFIG REQUIRED)
find_package(Boost REQUIRED COMPONENTS program_options dll)
find_package(flecs CONFIG REQUIRED)
find_package(Eigen3 REQUIRED NO_MODULE)

file(
  GLOB_RECURSE
  PROJECT_SOURCE_FILES
  ${PROJECT_SOURCE_DIR}/src/**.cpp
)

add_library(${PROJECT_NAME} STATIC ${PROJECT_SOURCE_FILES})
add_library(${PROJECT_NAMESPACE}::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

# Именованный модуль z13.core (C++20 modules), пилотный (Этап 1 плана).
# Разбит на подмодули: по одному на заголовочный файл из include/lib_core.
# Пока подключается параллельно со старыми хедерами: сам модуль никто
# не импортирует, поэтому существующая сборка lib_core не ломается.
target_sources(
  ${PROJECT_NAME}
  PUBLIC
  FILE_SET CXX_MODULES
    BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/modules
    FILES
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/z13.core.cppm
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/z13.core.core_types.cppm
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/z13.core.config.cppm
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/z13.core.math.cppm
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/z13.core.components.cppm
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/z13.core.flecs_utils.cppm
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/z13.core.core.cppm
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
  Eigen3::Eigen
)

target_link_libraries(
  ${PROJECT_NAME}
  PUBLIC
  flecs::flecs_static
  # spdlog::spdlog
  spdlog::spdlog_header_only
)

# set_target_properties(${PROJECT_NAME} PROPERTIES 
#     MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
# )

install(DIRECTORY include/ DESTINATION include/${PROJECT_NAMESPACE}-${PROJECT_VERSION}/)

install(TARGETS ${PROJECT_NAME}
        EXPORT ${PROJECT_NAME}Targets
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
        FILE_SET CXX_MODULES
          DESTINATION lib/${PROJECT_NAMESPACE}-${PROJECT_VERSION}/modules
)

install(EXPORT ${PROJECT_NAME}Targets
        FILE ${PROJECT_NAMESPACE}_${PROJECT_NAME}Targets.cmake
        NAMESPACE ${PROJECT_NAMESPACE}::
        DESTINATION lib/cmake/${PROJECT_NAMESPACE}-${PROJECT_VERSION}/
)
