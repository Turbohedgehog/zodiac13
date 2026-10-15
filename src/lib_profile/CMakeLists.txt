cmake_minimum_required(VERSION 3.21)

project(z13_profile)

# Header-only: Tracy zones for every library, the stateless ones without z13_core included.
add_library(${PROJECT_NAME} INTERFACE)
add_library(${PROJECT_NAMESPACE}::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

target_include_directories(
  ${PROJECT_NAME}
  INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include/${PROJECT_NAMESPACE}-${Z13_FULL_VERSION}>
)

if(Z13_PROFILE)
  find_package(Tracy CONFIG REQUIRED)
  target_compile_definitions(${PROJECT_NAME} INTERFACE Z13_PROFILE)
  target_link_libraries(${PROJECT_NAME} INTERFACE Tracy::TracyClient)
endif()

install(DIRECTORY include/ DESTINATION include/${PROJECT_NAMESPACE}-${Z13_FULL_VERSION}/)

install(TARGETS ${PROJECT_NAME}
        EXPORT ${PROJECT_NAME}Targets
)

install(EXPORT ${PROJECT_NAME}Targets
        FILE ${PROJECT_NAMESPACE}_${PROJECT_NAME}Targets.cmake
        NAMESPACE ${PROJECT_NAMESPACE}::
        DESTINATION lib/cmake/${PROJECT_NAMESPACE}-${Z13_FULL_VERSION}/
)
