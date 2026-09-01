cmake_minimum_required(VERSION 3.28)

project(z13_launcher CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Boost REQUIRED COMPONENTS dll)

add_library(${PROJECT_NAME} STATIC)
add_library(${PROJECT_NAMESPACE}::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

target_sources(${PROJECT_NAME}
  PRIVATE
    src/z13_launcher.cpp
  PUBLIC
    FILE_SET z13_launcher_modules TYPE CXX_MODULES
    BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/src
    FILES
      src/z13_launcher.cppm
)

target_link_libraries(${PROJECT_NAME}
  PRIVATE
  zodiac13::core
  Boost::dll
)
