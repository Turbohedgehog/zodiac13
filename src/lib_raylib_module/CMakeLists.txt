cmake_minimum_required(VERSION 3.21)

project(raylib_module)

set(CMAKE_CXX_STANDARD 23)

find_package(raylib CONFIG REQUIRED)   # used only as an rlgl render lib (no InitWindow)
find_package(SDL3 CONFIG REQUIRED)     # owns the window / GL context / event loop
find_package(imgui CONFIG REQUIRED)    # UI layer (sdl3 + opengl3 bindings)
find_package(Boost REQUIRED COMPONENTS dll)
find_package(Eigen3 REQUIRED NO_MODULE)
find_package(assimp CONFIG REQUIRED)

file(
  GLOB_RECURSE
  PROJECT_SOURCE_FILES
  ${PROJECT_SOURCE_DIR}/src/*.h
  ${PROJECT_SOURCE_DIR}/src/*.cpp
)

add_library(${PROJECT_NAME} SHARED ${PROJECT_SOURCE_FILES})
add_library(${PROJECT_NAMESPACE}::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

# input_publisher.cpp includes the flatbuffers-generated input_config_generated.h;
# the fbs_schemas INTERFACE lib doesn't reliably pull the generator, so depend on it.
add_dependencies(${PROJECT_NAME} GENERATE_FBS_SCHEMAS_HEADERS)

target_include_directories(
  ${PROJECT_NAME}
  PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include/${PROJECT_NAMESPACE}-${PROJECT_VERSION}>
  PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(${PROJECT_NAME} PRIVATE Z13_ASSETS_DIR="assets")

# raylib.h and <windows.h> both declare CloseWindow/ShowCursor/DrawText/Rectangle/...
# Drop the GDI/USER parts of windows.h (this module needs neither).
if(WIN32)
  target_compile_definitions(${PROJECT_NAME} PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX NOGDI NOUSER)
endif()

target_link_libraries(
  ${PROJECT_NAME}
  PRIVATE
  zodiac13::core
  zodiac13::components
  raylib
  SDL3::SDL3
  imgui::imgui
  Boost::dll
  Eigen3::Eigen
  assimp::assimp
)

set(MODULE_TARGET_PATH "modules/raylib")

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

# vcpkg's applocal step already deploys raylib.dll / glfw3.dll / ... next to
# raylib_module.dll; the exe loads it with LOAD_WITH_ALTERED_SEARCH_PATH, so the
# whole chain resolves from bin/modules/raylib/.

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
        ${CMAKE_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:zodiac13>/assets
)

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
