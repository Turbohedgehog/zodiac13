cmake_minimum_required(VERSION 3.21)

# file(TO_CMAKE_PATH $ENV{VCPKG_ROOT} VCPKG_ROOT)
set(VCPKG_MANIFEST_MODE ON)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CRT_LINKAGE static)
set(VCPKG_INSTALLED_DIR "${CMAKE_SOURCE_DIR}/../zodiac13_vcpkg_installed")

set(BUILD_SHARED_LIBS OFF)

# set(CMAKE_TOOLCHAIN_FILE "${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
set(CMAKE_TOOLCHAIN_FILE "vcpkg/scripts/buildsystems/vcpkg.cmake")

if(WIN32 AND DEFINED VCPKG_TARGET_TRIPLET)
  string(FIND "${VCPKG_TARGET_TRIPLET}" "-static" STATIC_POS)
  if(STATIC_POS EQUAL -1)
      set(VCPKG_TARGET_TRIPLET "${VCPKG_TARGET_TRIPLET}-static")
  endif()
endif()

project(zodiac13)

# message("~~~ CMAKE_TOOLCHAIN_FILE = ${CMAKE_TOOLCHAIN_FILE}")

set(CMAKE_INSTALL_PREFIX "${CMAKE_SOURCE_DIR}/install" CACHE PATH "Installation directory" FORCE)
set(ZODIAC_13_ROOT_DIR "${PROJECT_SOURCE_DIR}" CACHE PATH "Binary directory" FORCE)
set(PROJECT_NAMESPACE ${PROJECT_NAME} CACHE PATH "Project namespace" FORCE)
set(PROJECT_VERSION 0.0.1 CACHE PATH "Z13 version" FORCE)

# get_cmake_property(_variableNames VARIABLES)
# list (SORT _variableNames)
# foreach (_variableName ${_variableNames})
#     message(STATUS "=== ${_variableName}=${${_variableName}}")
# endforeach()

add_subdirectory(src/lib_core)
add_subdirectory(src/lib_z13)
add_subdirectory(src/lib_ogre_module)
add_subdirectory(src/zodiac13)

