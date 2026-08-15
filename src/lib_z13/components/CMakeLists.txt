# C++20 modules (FILE_SET CXX_MODULES) require CMake >= 3.28
cmake_minimum_required(VERSION 3.28)

project(components)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Boost REQUIRED COMPONENTS container)

# STATIC (а не INTERFACE), т.к. экспорт именованного модуля z13.components
# через FILE_SET CXX_MODULES требует компилируемой библиотеки.
add_library(${PROJECT_NAME} STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/dummy.cpp
)
add_library(${PROJECT_NAMESPACE}::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

# Именованный модуль z13.components (типы без flatbuffers-зависимостей).
# Типы из input.h, зависящие от сгенерированных flatbuffers-типов, остаются
# в классических заголовках (см. план миграции, раздел 4.3, вариант в).
target_sources(
  ${PROJECT_NAME}
  PUBLIC
  FILE_SET CXX_MODULES
    BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/modules
    FILES ${CMAKE_CURRENT_SOURCE_DIR}/modules/z13.components.cppm
)

target_include_directories(${PROJECT_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(
    ${PROJECT_NAME}
    PUBLIC
    zodiac13::fbs_schemas
    Boost::container
)

install(DIRECTORY include/ DESTINATION include/${PROJECT_NAMESPACE}-${PROJECT_VERSION}/)

install(TARGETS ${PROJECT_NAME}
        EXPORT ${PROJECT_NAME}Targets
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
        FILE_SET CXX_MODULES
          DESTINATION lib/${PROJECT_NAMESPACE}-${PROJECT_VERSION}/modules
)

install(EXPORT ${PROJECT_NAME}Targets
        FILE ${PROJECT_NAMESPACE}_${PROJECT_NAME}Targets.cmake
        NAMESPACE ${PROJECT_NAMESPACE}::
        DESTINATION lib/cmake/${PROJECT_NAMESPACE}-${PROJECT_VERSION}/
)