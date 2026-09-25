cmake_minimum_required(VERSION 3.21)

project(z13_module)

set(CMAKE_CXX_STANDARD 23)

find_package(Eigen3 REQUIRED NO_MODULE)
find_package(platform_folders REQUIRED)
find_package(Boost REQUIRED COMPONENTS dll)

file(
  GLOB_RECURSE
  PROJECT_SOURCE_FILES
  ${PROJECT_SOURCE_DIR}/src/*.cpp
)

# Compiled once, reused as object files by both the SHARED (real plugin) and
# STATIC (test-only twin, see below) targets -- CMAKE_POSITION_INDEPENDENT_CODE
# is ON globally (root CMakeLists.txt), so there's no PIC conflict between the
# two consumers, and this avoids compiling every z13_module .cpp twice on
# every build.
add_library(${PROJECT_NAME}_objects OBJECT ${PROJECT_SOURCE_FILES} ${PROTO_SRCS} ${PROTO_HDRS})

# Depend on the concrete flatc custom target, not the fbs_schemas INTERFACE lib:
# add_dependencies() on an INTERFACE library produces no build-order edge under the
# VS generator, so a fresh configure builds this before input_config_generated.h exists.
add_dependencies(${PROJECT_NAME}_objects GENERATE_FBS_SCHEMAS_HEADERS)

target_include_directories(
  ${PROJECT_NAME}_objects
  PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
  $<INSTALL_INTERFACE:include/${PROJECT_NAMESPACE}-${PROJECT_VERSION}>
)

target_link_libraries(
  ${PROJECT_NAME}_objects
  PUBLIC
  Eigen3::Eigen
  PRIVATE
  zodiac13::core
  zodiac13::components
  sago::platform_folders
  Boost::dll
)

add_library(${PROJECT_NAME} SHARED $<TARGET_OBJECTS:${PROJECT_NAME}_objects>)
add_library(${PROJECT_NAMESPACE}::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

# Another plugin (net_module) links this one for SpawnPlayer and friends. MSVC exports
# nothing by default, unlike ELF, so let CMake generate the .def instead of annotating
# the public headers -- the object files are shared with the static test twin, which must
# not carry dllexport.
set_target_properties(
  ${PROJECT_NAME} PROPERTIES
  PROTO_INCLUDE_DIR ${CMAKE_CURRENT_BINARY_DIR}
  WINDOWS_EXPORT_ALL_SYMBOLS ON
)

# PUBLIC: camera_look.h (a public header) uses Eigen::Matrix4f, so consumers
# linking z13_module need Eigen's include paths too.
target_link_libraries(
  ${PROJECT_NAME}
  PUBLIC
  ${PROJECT_NAME}_objects
  Eigen3::Eigen
  PRIVATE
  zodiac13::core
  zodiac13::components
  sago::platform_folders
  Boost::dll
)

# Test-only static twin of the SHARED target above, same object files. Lets
# test binaries link z13_module directly into their own process instead of
# across a .so boundary -- needed because flecs (statically linked into
# zodiac13::core) keeps process-global internal state that isn't shared
# correctly between independently-linked shared objects. Never installed;
# not part of the plugin API surface ModuleLibHolder loads at runtime.
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
  sago::platform_folders
  Boost::dll
)

# set_target_properties(${PROJECT_NAME} PROPERTIES
#     MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
# )

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

SET(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "${OUTPUT_DIRECTORY}")
SET(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${OUTPUT_DIRECTORY}")

set(MODULE_TARGET_PATH "modules/z13_module")

# RUNTIME_OUTPUT_DIRECTORY places the .dll on Windows; on Linux/macOS a SHARED
# target's .so/.dylib is placed via LIBRARY_OUTPUT_DIRECTORY instead, and would
# otherwise land in the default per-target build dir, not next to the exe where
# ModuleLibHolder looks for it. PREFIX "" drops the "lib" CMake adds by default
# on Unix, since the module path in z13_config.yaml has no such prefix.
set_target_properties(${PROJECT_NAME} PROPERTIES
    PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/${MODULE_TARGET_PATH}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/bin/${MODULE_TARGET_PATH}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/bin/${MODULE_TARGET_PATH}
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/${MODULE_TARGET_PATH}
    LIBRARY_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/bin/${MODULE_TARGET_PATH}
    LIBRARY_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/bin/${MODULE_TARGET_PATH}
)