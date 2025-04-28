cmake_minimum_required(VERSION 3.21)

project(z13_launcher)

set(CMAKE_CXX_STANDARD 20)

# find_package(z13::core REQUIRED)

# set(CMAKE_INSTALL_PREFIX "${CMAKE_SOURCE_DIR}/install" CACHE PATH "Installation directory" FORCE)

# file(
#   GLOB_RECURSE
#   PROJECT_HEADER_FILES
#   ${PROJECT_SOURCE_DIR}/*.h
# )

file(
  GLOB_RECURSE
  PROJECT_SOURCE_FILES
  ${PROJECT_SOURCE_DIR}/src/*.h
  ${PROJECT_SOURCE_DIR}/src/*.cpp
)

add_library(${PROJECT_NAME} STATIC ${PROJECT_SOURCE_FILES})
add_library(${PROJECT_NAMESPACE}::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

target_include_directories(
  ${PROJECT_NAME}
  PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include/${PROJECT_NAMESPACE}-${PROJECT_VERSION}>
  # ${Z13_CORE_INCLUDE_DIR}
)

target_link_libraries(
  ${PROJECT_NAME}
  PRIVATE
  zodiac13::core
  zodiac13::z13_module
  zodiac13::ogre_module
  # ${Z13_CORE_LIB}
)

# include_directories(
#   ${PROJECT_SOURCE_DIR}
#   ${ZODIAC_13_ROOT_DIR}/flecs/include
# )

# set(Z13_INCLUDE_DIR ${PROJECT_SOURCE_DIR} CACHE PATH "z13 lib include dirs" FORCE)
# set(Z13_LIB ${PROJECT_NAME} CACHE PATH "z13 lib" FORCE)

install(DIRECTORY include/ DESTINATION include/${PROJECT_NAMESPACE}-${PROJECT_VERSION}/)

install(TARGETS ${PROJECT_NAME}
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

