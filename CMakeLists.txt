cmake_minimum_required(VERSION 3.21)

file(TO_CMAKE_PATH $ENV{VCPKG_ROOT} VCPKG_ROOT)
set(VCPKG_INSTALLED_DIR "${CMAKE_SOURCE_DIR}/vcpkg_installed")

# MSVC2026 building fix
set(VCPKG_INSTALL_OPTIONS "--clean-after-build")
set(VCPKG_LINKER_FLAGS_DEBUG "/INCREMENTAL:NO")
if(MSVC)
    # Ищем флаг /ZI и заменяем его на /Zi в строке флагов для Debug
    string(REPLACE "/ZI" "/Zi" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    string(REPLACE "/ZI" "/Zi" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
endif()

set(CMAKE_TOOLCHAIN_FILE "${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")

# if(WIN32 AND DEFINED VCPKG_TARGET_TRIPLET)
#   string(FIND "${VCPKG_TARGET_TRIPLET}" "-static" STATIC_POS)
#   if(STATIC_POS EQUAL -1)
#       set(VCPKG_TARGET_TRIPLET "${VCPKG_TARGET_TRIPLET}-static")
#   endif()
# endif()

file(READ "${CMAKE_SOURCE_DIR}/version.txt" VER_BASE)
string(STRIP "${VER_BASE}" VER_BASE) # Убираем лишние пробелы/переносы
string(REPLACE "." ";" VER_LIST ${VER_BASE})
list(GET VER_LIST 0 Z13_VERSION_MAJOR)
list(GET VER_LIST 1 Z13_VERSION_MINOR)
set(Z13_VERSION_PATCH 0 CACHE STRING "Patch version from CI")
# Which kind of build this is, shown after the version: "-pr12" for a pull request; empty for a release.
set(Z13_VERSION_SUFFIX "" CACHE STRING "Suffix of the shown version, from CI")

set(Z13_FULL_VERSION "${Z13_VERSION_MAJOR}.${Z13_VERSION_MINOR}.${Z13_VERSION_PATCH}")
# Shown in the game's corner: CI passes the day it builds, a local build the day it configured.
if(NOT DEFINED Z13_BUILD_DATE)
    string(TIMESTAMP Z13_BUILD_DATE "%Y-%m-%d" UTC)
endif()

project(zodiac13 VERSION ${Z13_FULL_VERSION})

# set(CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_DEBUG} /INCREMENTAL:NO")
# set(CMAKE_SHARED_LINKER_FLAGS_DEBUG "${CMAKE_SHARED_LINKER_FLAGS_DEBUG} /INCREMENTAL:NO")


set(CMAKE_INSTALL_PREFIX "${CMAKE_SOURCE_DIR}/install" CACHE PATH "Installation directory" FORCE)
set(PROJECT_NAMESPACE ${PROJECT_NAME} CACHE PATH "Project namespace" FORCE)


# get_cmake_property(_variableNames VARIABLES)
# list (SORT _variableNames)
# foreach (_variableName ${_variableNames})
#     message(STATUS "=== ${_variableName}=${${_variableName}}")
# endforeach()

# Benchmarks (tests/bench/) run for minutes and only matter when measuring: `make.py --bench`.
option(Z13_BUILD_BENCHMARKS "Build the benchmark tests under tests/bench/" OFF)
# Tracy zones (lib_profile/profile.h); off, they compile to nothing.
option(Z13_PROFILE "Build with Tracy profiler zones" OFF)
# Off: no raylib_module, so zodiac13 runs only as a windowless dedicated server (--server, whose
# config loads no renderer). For machines without a display; CI builds the renderer.
option(Z13_BUILD_RENDER "Build the render plugin (raylib_module)" ON)
option(Z13_DEBUG_OPTIMIZE "Build the math libraries with -O1 in Debug (GCC, Clang)" ON)
if(ANDROID AND NOT Z13_BUILD_RENDER)
    message(FATAL_ERROR "The Android app needs the renderer: Z13_BUILD_RENDER must be ON")
endif()
# One per build directory, so a Release build (make.py -br: bin-release/) doesn't overwrite Debug.
set(Z13_BIN_DIR "${CMAKE_SOURCE_DIR}/bin" CACHE PATH "Where the executable, plugins, tests, config/ and assets/ go")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_COMPILE_WARNING_AS_ERROR ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Precompiled flecs/Eigen/gtest cut a module source's compile time by ~45 % (GCC, PRoot). On by
# default only for GCC: sccache can't cache MSVC's, and the clang job, built without them,
# catches a source that relies on a precompiled header instead of including it.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(Z13_PRECOMPILE_HEADERS_DEFAULT ON)
else()
    set(Z13_PRECOMPILE_HEADERS_DEFAULT OFF)
endif()
option(Z13_PRECOMPILE_HEADERS "Precompile the heavy headers most sources include" ${Z13_PRECOMPILE_HEADERS_DEFAULT})

# Local builds pick up ccache when installed; CI passes its own launcher.
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM AND NOT CMAKE_CXX_COMPILER_LAUNCHER)
    set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
endif()
# ccache caches a source built with a precompiled header only under this sloppiness.
if(Z13_PRECOMPILE_HEADERS AND CMAKE_CXX_COMPILER_LAUNCHER MATCHES "ccache")
    list(APPEND CMAKE_CXX_COMPILER_LAUNCHER "sloppiness=pch_defines,time_macros")
endif()

# lld where the toolchain takes it: linking the Debug test library took 10.8 s with GNU ld and
# 4.2 s with lld (PRoot, arm64), and nearly every edit relinks it.
if(NOT MSVC)
    include(CheckLinkerFlag)
    check_linker_flag(CXX "-fuse-ld=lld" Z13_HAVE_LLD)
    if(Z13_HAVE_LLD)
        add_link_options(-fuse-ld=lld)
    endif()
endif()

# Lockstep simulation needs matching float results across participants: no FMA
# contraction (GCC's gnu++ default fuses on arm64). Bullet gets the same via ports/bullet3.
if(MSVC)
    add_compile_options(/fp:precise /MP)
else()
    add_compile_options(-ffp-contract=off)
endif()

include(cmake/z13_targets.cmake)
include(cmake/z13_schemas.cmake)

add_subdirectory(src/lib_profile)
add_subdirectory(src/lib_core)
add_subdirectory(src/lib_z13)
add_subdirectory(src/zodiac13)
if(Z13_BUILD_RENDER)
    add_subdirectory(src/lib_raylib_module)
endif()
add_subdirectory(src/lib_test_dll)

# Android builds the app alone: the tests run on the desktop platforms.
if(NOT ANDROID)
    enable_testing()
    add_subdirectory(src/tests)
endif()

# config/ is copied next to the binaries on every build and read from there at startup.
add_custom_target(z13_config ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/config
            ${Z13_BIN_DIR}/config
    COMMENT "Copying config/ -> bin/config/"
    VERBATIM
)
# assets/ likewise: the renderer and building_module (its block palette) both read it.
add_custom_target(z13_assets ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
            ${CMAKE_SOURCE_DIR}/assets
            ${Z13_BIN_DIR}/assets
    COMMENT "Copying assets/ -> bin/assets/"
    VERBATIM
)
add_dependencies(building_module z13_assets)
if(Z13_BUILD_RENDER)
    add_dependencies(raylib_module z13_assets)
endif()
if(NOT ANDROID)
    add_dependencies(z13_tests z13_assets)
    add_dependencies(z13_plugin_smoke z13_assets)
endif()

# The exe loads these DLLs at runtime (not linked), so make building it also
# build them — otherwise `--target zodiac13` / the IDE build produces a useless exe.
include(cmake/plugins.cmake)
add_dependencies(zodiac13 z13_config ${Z13_PLUGINS})

if(ANDROID)
    include(cmake/android.cmake)
else()
    include(cmake/packaging.cmake)
endif()

