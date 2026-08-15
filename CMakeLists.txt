# C++20 modules (FILE_SET CXX_MODULES) require CMake >= 3.28
cmake_minimum_required(VERSION 3.28)

file(TO_CMAKE_PATH $ENV{VCPKG_ROOT} VCPKG_ROOT)
set(VCPKG_INSTALLED_DIR "${CMAKE_SOURCE_DIR}/vcpkg_installed")

# MSVC2026 + Ogre Eternal building fix
set(VCPKG_INSTALL_OPTIONS "--clean-after-build")
set(VCPKG_LINKER_FLAGS_DEBUG "/INCREMENTAL:NO")
if(MSVC)
    # Ищем флаг /ZI и заменяем его на /Zi в строке флагов для Debug
    string(REPLACE "/ZI" "/Zi" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    string(REPLACE "/ZI" "/Zi" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
endif()

# Отключаем /utf-8 из vcpkg toolchain: он задаётся через CMAKE_CXX_FLAGS и при
# использовании C++20/23 модулей (FILE_SET CXX_MODULES) не попадает в отдельную
# командную строку компиляции .cppm-файлов, из-за чего MSVC выдаёт C5050
# (несовместимая среда при импорте модуля). Флаг задаём единообразно ниже
# через add_compile_options, который применяется ко всем source-файлам цели.
set(VCPKG_SET_CHARSET_FLAG OFF)
set(CMAKE_TOOLCHAIN_FILE "${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")

# vcpkg + boost warning fix
if(POLICY CMP0167)
    cmake_policy(SET CMP0167 OLD)
endif()

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

set(CMAKE_INSTALL_PREFIX "${CMAKE_SOURCE_DIR}/install" CACHE PATH "Installation directory" FORCE)
set(PROJECT_NAMESPACE ${PROJECT_NAME} CACHE PATH "Project namespace" FORCE)
set(PROJECT_VERSION 0.0.1 CACHE PATH "Z13 version" FORCE)

# Единообразная кодировка исходников UTF-8 для всех целей, включая цели с
# C++20/23 модулями. add_compile_options применяется ко всем source-файлам цели
# (в т.ч. .cppm из FILE_SET CXX_MODULES), что исключает MSVC C5050.
if(MSVC)
    add_compile_options(/utf-8)
endif()

# get_cmake_property(_variableNames VARIABLES)
# list (SORT _variableNames)
# foreach (_variableName ${_variableNames})
#     message(STATUS "=== ${_variableName}=${${_variableName}}")
# endforeach()

# set(CMAKE_COMPILE_WARNING_AS_ERROR ON)

add_subdirectory(src/lib_core)
add_subdirectory(src/lib_z13)
# add_subdirectory(src/zodiac13)
# add_subdirectory(src/lib_ogre_module)
# add_subdirectory(src/lib_test_dll)

set(CPACK_GENERATOR "DEB;TGZ")

set(CPACK_PACKAGE_VERSION_MAJOR ${Z13_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${Z13_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${Z13_VERSION_PATCH})
set(CPACK_PACKAGE_FILE_NAME "${PROJECT_NAME}-${Z13_FULL_VERSION}")
set(CPACK_PACKAGE_CONTACT unlinker@mail.ru)

include(CPack)

