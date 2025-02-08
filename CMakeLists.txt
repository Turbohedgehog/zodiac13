cmake_minimum_required(VERSION 3.13)

project(Zodiac13)

# set(CMAKE_CXX_STANDARD 20)
# set(CMAKE_CXX_STANDARD 11)

option(FLECS_SHARED "Build shared flecs lib" OFF)

add_subdirectory(flecs)


add_subdirectory(ogre-next-deps)

set(OGRE_BUILD_SAMPLES2 FALSE CACHE BOOL "Build Ogre new demos (2.1)" FORCE)

add_subdirectory(ogre-next)

# Scan and append project files
file(
  GLOB_RECURSE
  PROJECT_SOURCE_FILES
  ${PROJECT_SOURCE_DIR}/src/*.h
  ${PROJECT_SOURCE_DIR}/src/*.cpp
)

include_directories(
  ${PROJECT_SOURCE_DIR}/src
  ${PROJECT_SOURCE_DIR}/flecs/include
  ${PROJECT_SOURCE_DIR}/ogre-next/OgreMain/include)

add_executable(${PROJECT_NAME} ${PROJECT_SOURCE_FILES})
target_link_libraries(${PROJECT_NAME} flecs::flecs_static ${OGRE_LIBRARIES})



install(TARGETS ${PROJECT_NAME} DESTINATION ${PROJECT_SOURCE_DIR}/bin)
