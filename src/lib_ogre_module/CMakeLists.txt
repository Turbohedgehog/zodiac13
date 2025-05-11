cmake_minimum_required(VERSION 3.21)

project(ogre_module)

set(CMAKE_CXX_STANDARD 20)

SET(OGRE_STATIC ON)

find_package(SDL2 CONFIG REQUIRED)
find_package(OGRE CONFIG REQUIRED)

file(TO_CMAKE_PATH ${OGRE_PLUGIN_DIR} OGRE_PLUGIN_DIR_VAR)
message("~~~ OGRE_PLUGIN_DIR_VAR = ${OGRE_PLUGIN_DIR_VAR}")
add_compile_definitions(OGRE_PLUGIN_DIR=\"${OGRE_PLUGIN_DIR_VAR}\")

file(
  GLOB_RECURSE
  PROJECT_SOURCE_FILES
  ${PROJECT_SOURCE_DIR}/src/*.h
  ${PROJECT_SOURCE_DIR}/src/*.cpp
)

add_library(${PROJECT_NAME} STATIC ${PROJECT_SOURCE_FILES})
add_library(${PROJECT_NAMESPACE}::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

target_include_directories(
  ${PROJECT_NAME}
  PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include/${PROJECT_NAMESPACE}-${PROJECT_VERSION}>
)

# get_cmake_property(_variableNames VARIABLES)
# list (SORT _variableNames)
# foreach (_variableName ${_variableNames})
#     message(STATUS "=== ${_variableName}=${${_variableName}}")
# endforeach()

message("~~~ OGRE_PLUGINS = ${OGRE_PLUGINS}")

target_link_libraries(
  ${PROJECT_NAME}
  PRIVATE
  zodiac13::core
  zodiac13::components
  SDL2::SDL2
  OgreMain
)

install(DIRECTORY include/ DESTINATION include/${PROJECT_NAMESPACE}-${PROJECT_VERSION}/)

install(TARGETS ${PROJECT_NAME}
        EXPORT ${PROJECT_NAME}Targets
        RUNTIME_DEPENDENCY_SET runtime_deps
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
)


# Копирует зависимости, но с избытком
# install(IMPORTED_RUNTIME_ARTIFACTS SDL2::SDL2 ${OGRE_LIBRARIES}
#         DESTINATION ${ZODIAC_13_ROOT_DIR}/bin
# )

install(EXPORT ${PROJECT_NAME}Targets
        FILE ${PROJECT_NAMESPACE}_${PROJECT_NAME}Targets.cmake
        NAMESPACE ${PROJECT_NAMESPACE}::
        DESTINATION lib/cmake/${PROJECT_NAMESPACE}-${PROJECT_VERSION}/
)
