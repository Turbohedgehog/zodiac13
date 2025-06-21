cmake_minimum_required(VERSION 3.21)

project(z13_launcher)

set(CMAKE_CXX_STANDARD 20)


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

target_link_libraries(
  ${PROJECT_NAME}
  PRIVATE
  zodiac13::core
  zodiac13::z13_module
  zodiac13::ogre_module
)

# set_target_properties(${PROJECT_NAME} PROPERTIES 
#     MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
# )

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

