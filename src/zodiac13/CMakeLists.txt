cmake_minimum_required(VERSION 3.21)

project(zodiac13)

find_package(flecs CONFIG REQUIRED)

set(CMAKE_CXX_STANDARD 20)

add_executable(${PROJECT_NAME} main.cpp)

message("~~~~ CMAKE_CURRENT_SOURCE_DIR = ${CMAKE_CURRENT_SOURCE_DIR}")

# target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
# include_directories(${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(
  ${PROJECT_NAME}
  PRIVATE
  zodiac13::z13_launcher
  flecs::flecs_static
)

set_target_properties(${PROJECT_NAME} PROPERTIES 
    # RUNTIME_OUTPUT_DIRECTORY ${ZODIAC_13_ROOT_DIR}/bin
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)

install(TARGETS ${PROJECT_NAME}
        # RUNTIME_DEPENDENCY_SET runtime_deps
        DESTINATION ${ZODIAC_13_ROOT_DIR}/bin)
