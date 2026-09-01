cmake_minimum_required(VERSION 3.28)

project(z13_module CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Eigen3 REQUIRED NO_MODULE)
find_package(platform_folders REQUIRED)
find_package(Boost REQUIRED COMPONENTS dll)

add_library(${PROJECT_NAME} SHARED)
add_library(${PROJECT_NAMESPACE}::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

target_sources(${PROJECT_NAME}
  PRIVATE
    src/z13_module_factory.cpp
    src/toos/z13_environment.cpp
    src/input/input_config_loader.cpp
    src/bootstrap/bootstrap_system.cpp
    src/gameplay/gameplay_system.cpp
    src/building/building_system.cpp
    src/building/building_input_system.cpp
    src/input/gameplay_input_system.cpp
    src/z13_module.cpp
  PRIVATE
    FILE_SET z13_module_modules TYPE CXX_MODULES
    BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/src
    FILES
      src/bootstrap/bootstrap_system.cppm
      src/gameplay/gameplay_system.cppm
      src/building/building_system.cppm
      src/building/building_input_system.cppm
      src/input/gameplay_input_system.cppm
      src/z13_module.cppm
      src/z13_module_main.cppm
)

target_include_directories(
  ${PROJECT_NAME}
  PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
  $<INSTALL_INTERFACE:include/${PROJECT_NAMESPACE}-${PROJECT_VERSION}>
)

set_target_properties(
  ${PROJECT_NAME} PROPERTIES
  PROTO_INCLUDE_DIR ${CMAKE_CURRENT_BINARY_DIR}
)

target_link_libraries(
  ${PROJECT_NAME}
  PRIVATE
  zodiac13::core
  zodiac13::components
  Eigen3::Eigen
  sago::platform_folders
  Boost::dll
)


# set_target_properties(${PROJECT_NAME} PROPERTIES 
#     MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
# )

install(DIRECTORY include/ DESTINATION include/${PROJECT_NAMESPACE}-${PROJECT_VERSION}/)

install(TARGETS ${PROJECT_NAME}
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
)

SET(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "${OUTPUT_DIRECTORY}")
SET(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${OUTPUT_DIRECTORY}")

set(MODULE_TARGET_PATH "modules/z13_module")

set_target_properties(${PROJECT_NAME} PROPERTIES 
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/${MODULE_TARGET_PATH}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/bin/${MODULE_TARGET_PATH}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/bin/${MODULE_TARGET_PATH}
)