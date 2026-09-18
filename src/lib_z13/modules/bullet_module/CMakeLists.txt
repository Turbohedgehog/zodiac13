cmake_minimum_required(VERSION 3.21)

project(bullet_module)

set(CMAKE_CXX_STANDARD 23)

find_package(Bullet REQUIRED)
find_package(Eigen3 REQUIRED NO_MODULE)
find_package(Boost REQUIRED COMPONENTS dll)

file(
  GLOB_RECURSE
  PROJECT_SOURCE_FILES
  ${PROJECT_SOURCE_DIR}/src/*.h
  ${PROJECT_SOURCE_DIR}/src/*.cpp
)

# Shared by the SHARED plugin and the test-only STATIC twin (rationale: z13_module/CMakeLists.txt).
add_library(${PROJECT_NAME}_objects OBJECT ${PROJECT_SOURCE_FILES})

target_include_directories(
  ${PROJECT_NAME}_objects
  PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<BUILD_INTERFACE:${BULLET_INCLUDE_DIRS}>
  $<INSTALL_INTERFACE:include/${PROJECT_NAMESPACE}-${PROJECT_VERSION}>
)

target_link_libraries(
  ${PROJECT_NAME}_objects
  PUBLIC
  Eigen3::Eigen
  PRIVATE
  zodiac13::core
  zodiac13::components
  ${BULLET_LIBRARIES}
  Boost::dll
)

add_library(${PROJECT_NAME} SHARED $<TARGET_OBJECTS:${PROJECT_NAME}_objects>)
add_library(${PROJECT_NAMESPACE}::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

target_link_libraries(
  ${PROJECT_NAME}
  PUBLIC
  ${PROJECT_NAME}_objects
  Eigen3::Eigen
  PRIVATE
  zodiac13::core
  zodiac13::components
  ${BULLET_LIBRARIES}
  Boost::dll
)

# Test-only static twin (see z13_module_static). Never installed.
add_library(${PROJECT_NAME}_static STATIC $<TARGET_OBJECTS:${PROJECT_NAME}_objects>)
add_library(${PROJECT_NAMESPACE}::${PROJECT_NAME}_static ALIAS ${PROJECT_NAME}_static)
target_link_libraries(
  ${PROJECT_NAME}_static
  PUBLIC
  ${PROJECT_NAME}_objects
  Eigen3::Eigen
  PRIVATE
  zodiac13::core
  zodiac13::components
  ${BULLET_LIBRARIES}
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

set(MODULE_TARGET_PATH "modules/bullet_module")
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
