cmake_minimum_required(VERSION 3.21)

message("~~~ CMAKE_TOOLCHAIN_FILE = ${CMAKE_TOOLCHAIN_FILE}")

file(TO_CMAKE_PATH $ENV{VCPKG_ROOT} VCPKG_ROOT)
# set(VCPKG_ROOT $ENV{VCPKG_ROOT})
message("~~~~ VCPKG_ROOT = ${VCPKG_ROOT}")
message("~~~~ USERNAME = ${USERNAME}")

set(CMAKE_TOOLCHAIN_FILE "${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
# set(BUILD_SHARED_LIBS OFF)
# include(${CMAKE_TOOLCHAIN_FILE})

project(zodiac13)

message("~~~ CMAKE_TOOLCHAIN_FILE = ${CMAKE_TOOLCHAIN_FILE}")

set(CMAKE_INSTALL_PREFIX "${CMAKE_SOURCE_DIR}/install" CACHE PATH "Installation directory" FORCE)
set(ZODIAC_13_ROOT_DIR "${PROJECT_SOURCE_DIR}" CACHE PATH "Binary directory" FORCE)
set(PROJECT_NAMESPACE ${PROJECT_NAME} CACHE PATH "Project namespace" FORCE)
set(PROJECT_VERSION 0.0.1 CACHE PATH "Z13 version" FORCE)


add_subdirectory(src/lib_core)
add_subdirectory(src/lib_z13)
add_subdirectory(src/lib_ogre_module)
add_subdirectory(src/zodiac13)

