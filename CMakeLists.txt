cmake_minimum_required(VERSION 3.13)

project(zodiac13)

set(CMAKE_INSTALL_PREFIX "${CMAKE_SOURCE_DIR}/install" CACHE PATH "Installation directory" FORCE)
set(ZODIAC_13_ROOT_DIR "${PROJECT_SOURCE_DIR}" CACHE PATH "Binary directory" FORCE)

# option(FLECS_SHARED "Build shared flecs lib" OFF)
# add_subdirectory(flecs)
# add_subdirectory(ogre-next-deps)
# set(OGRE_BUILD_SAMPLES2 FALSE CACHE BOOL "Build Ogre new demos (2.1)" FORCE)
# add_subdirectory(ogre-next)

# set (Boost_ROOT "C:/Projects/boost")
# set (BOOST_ROOT ${PROJECT_SOURCE_DIR}/../../../boost)
# set (BOOST_INCLUDE_DIR ${BOOST_ROOT})
# set (Boost_NO_SYSTEM_PATHS ON)
# set (Boost_USE_MULTITHREADED ON)
# set (Boost_USE_STATIC_LIBS ON)
# set (Boost_USE_STATIC_RUNTIME OFF)
# set (BOOST_ALL_DYN_LINK OFF)

add_subdirectory(src/lib_core)
add_subdirectory(src/lib_z13)
add_subdirectory(src/zodiac13)

# Scan and append project files
# file(
#   GLOB_RECURSE
#   PROJECT_SOURCE_FILES
#   ${PROJECT_SOURCE_DIR}/src/*.h
#   ${PROJECT_SOURCE_DIR}/src/*.cpp
# )

include_directories(
  # ${PROJECT_SOURCE_DIR}/src
  # ${PROJECT_SOURCE_DIR}/flecs/include
  ${PROJECT_SOURCE_DIR}/ogre-next/OgreMain/include)

# add_executable(${PROJECT_NAME} ${PROJECT_SOURCE_FILES})
# target_link_libraries(${PROJECT_NAME} flecs::flecs_static ${OGRE_LIBRARIES})

# install(TARGETS ${PROJECT_NAME} DESTINATION ${PROJECT_SOURCE_DIR}/bin)
