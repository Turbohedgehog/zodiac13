project(z13_profile)

# Header-only: Tracy zones for every library, the stateless ones without z13_core included.
z13_add_library(${PROJECT_NAME} INTERFACE)

if(Z13_PROFILE)
  find_package(Tracy CONFIG REQUIRED)
  target_compile_definitions(${PROJECT_NAME} INTERFACE Z13_PROFILE)
  target_link_libraries(${PROJECT_NAME} INTERFACE Tracy::TracyClient)
endif()
