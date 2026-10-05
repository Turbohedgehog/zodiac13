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

set(Z13_FULL_VERSION "${Z13_VERSION_MAJOR}.${Z13_VERSION_MINOR}.${Z13_VERSION_PATCH}")

project(zodiac13 VERSION ${Z13_FULL_VERSION})

# set(CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_DEBUG} /INCREMENTAL:NO")
# set(CMAKE_SHARED_LINKER_FLAGS_DEBUG "${CMAKE_SHARED_LINKER_FLAGS_DEBUG} /INCREMENTAL:NO")


set(CMAKE_INSTALL_PREFIX "${CMAKE_SOURCE_DIR}/install" CACHE PATH "Installation directory" FORCE)
set(PROJECT_NAMESPACE ${PROJECT_NAME} CACHE PATH "Project namespace" FORCE)
set(PROJECT_VERSION 0.2.0 CACHE PATH "Z13 version" FORCE)


# get_cmake_property(_variableNames VARIABLES)
# list (SORT _variableNames)
# foreach (_variableName ${_variableNames})
#     message(STATUS "=== ${_variableName}=${${_variableName}}")
# endforeach()

# Benchmarks (tests/bench/) run for minutes and only matter when measuring: `make.py --bench`.
option(Z13_BUILD_BENCHMARKS "Build the benchmark tests under tests/bench/" OFF)

set(CMAKE_COMPILE_WARNING_AS_ERROR ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Local builds pick up ccache when installed; CI passes its own launcher.
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM AND NOT CMAKE_CXX_COMPILER_LAUNCHER)
    set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
endif()

# Lockstep simulation needs matching float results across participants: no FMA
# contraction (GCC's gnu++ default fuses on arm64). Bullet gets the same via ports/bullet3.
if(MSVC)
    add_compile_options(/fp:precise)
else()
    add_compile_options(-ffp-contract=off)
endif()

add_subdirectory(src/lib_core)
add_subdirectory(src/lib_z13)
add_subdirectory(src/zodiac13)
add_subdirectory(src/lib_raylib_module)
add_subdirectory(src/lib_test_dll)

enable_testing()
add_subdirectory(src/tests)

# config/ is copied next to the binaries on every build and read from there at startup.
add_custom_target(z13_config ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/config
            ${CMAKE_SOURCE_DIR}/bin/config
    COMMENT "Copying config/ -> bin/config/"
    VERBATIM
)
# The exe loads these DLLs at runtime (not linked), so make building it also
# build them — otherwise `--target zodiac13` / the IDE build produces a useless exe.
add_dependencies(zodiac13 z13_config z13_module net_module bullet_module station_module raylib_module test_dll_module)

include(cmake/packaging.cmake)

