cmake_minimum_required(VERSION 3.21)

project(building_module)

set(CMAKE_CXX_STANDARD 23)

find_package(Eigen3 REQUIRED NO_MODULE)
find_package(Boost REQUIRED COMPONENTS dll)

set(
  PROJECT_SOURCE_FILES
  src/asset_file.cpp
  src/block_building_system.cpp
  src/block_cutting.cpp
  src/block_entities.cpp
  src/block_snapshot_system.cpp
  src/block_watch.cpp
  src/brush_aim.cpp
  src/brush_system.cpp
  src/build_action_ids.cpp
  src/build_validation.cpp
  src/building_input_system.cpp
  src/building_module.cpp
  src/building_module_factory.cpp
  src/building_system.cpp
  src/palette_system.cpp
  src/station_scene_system.cpp
)

add_library(${PROJECT_NAME}_objects OBJECT ${PROJECT_SOURCE_FILES})

# building_generated.h comes from the concrete flatc target (see z13_module/CMakeLists.txt).
add_dependencies(${PROJECT_NAME}_objects GENERATE_FBS_SCHEMAS_HEADERS)

target_include_directories(
  ${PROJECT_NAME}_objects
  PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include/${PROJECT_NAMESPACE}-${PROJECT_VERSION}>
)

target_link_libraries(
  ${PROJECT_NAME}_objects
  PUBLIC
  Eigen3::Eigen
  PRIVATE
  zodiac13::core
  zodiac13::components
  zodiac13::z13_module
  zodiac13::z13_settings
  zodiac13::primitives
  zodiac13::z13_grid
  Boost::dll
)

add_library(${PROJECT_NAME} SHARED $<TARGET_OBJECTS:${PROJECT_NAME}_objects>)
add_library(${PROJECT_NAMESPACE}::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

# The tests link the plugin directly and use its internal symbols; MSVC exports nothing by default.
set_target_properties(${PROJECT_NAME} PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)

target_link_libraries(
  ${PROJECT_NAME}
  PUBLIC
  ${PROJECT_NAME}_objects
  Eigen3::Eigen
  PRIVATE
  zodiac13::core
  zodiac13::components
  zodiac13::z13_module
  zodiac13::z13_settings
  zodiac13::primitives
  zodiac13::z13_grid
  Boost::dll
)

install(DIRECTORY include/ DESTINATION include/${PROJECT_NAMESPACE}-${PROJECT_VERSION}/)

install(TARGETS ${PROJECT_NAME} ${PROJECT_NAME}_objects
        EXPORT ${PROJECT_NAME}Targets
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
)

install(EXPORT ${PROJECT_NAME}Targets
        FILE ${PROJECT_NAMESPACE}_${PROJECT_NAME}Targets.cmake
        NAMESPACE ${PROJECT_NAMESPACE}::
        DESTINATION lib/cmake/${PROJECT_NAMESPACE}-${PROJECT_VERSION}/
)

set(MODULE_TARGET_PATH "modules/building_module")
set(MODULE_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/bin/${MODULE_TARGET_PATH}")

# Output layout matches z13_module/CMakeLists.txt and the path in z13_config.yaml.
set_target_properties(${PROJECT_NAME} PROPERTIES
    PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY ${MODULE_OUTPUT_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${MODULE_OUTPUT_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${MODULE_OUTPUT_DIR}
    LIBRARY_OUTPUT_DIRECTORY ${MODULE_OUTPUT_DIR}
    LIBRARY_OUTPUT_DIRECTORY_DEBUG ${MODULE_OUTPUT_DIR}
    LIBRARY_OUTPUT_DIRECTORY_RELEASE ${MODULE_OUTPUT_DIR}
)
