cmake_minimum_required(VERSION 3.21)

project(net_module)

set(CMAKE_CXX_STANDARD 23)

find_package(unofficial-enet CONFIG REQUIRED)
find_package(Boost REQUIRED COMPONENTS dll)

# vcpkg's enet links these itself only for MinGW.
if(WIN32)
  set(ENET_PLATFORM_LIBS winmm ws2_32)
endif()

set(
  PROJECT_SOURCE_FILES
  src/catch_up.cpp
  src/client_session.cpp
  src/clock_sync.cpp
  src/enet_transport.cpp
  src/in_memory_transport.cpp
  src/net_action_sender.cpp
  src/net_module.cpp
  src/net_module_factory.cpp
  src/net_session.cpp
  src/net_session_system.cpp
  src/protocol.cpp
  src/remote_input.cpp
  src/scheduled_commands.cpp
  src/server_session.cpp
  src/session_control.cpp
  src/session_deltas.cpp
  src/session_settings.cpp
  src/state_digest.cpp
  src/wire_records.cpp
)

add_library(${PROJECT_NAME}_objects OBJECT ${PROJECT_SOURCE_FILES})

# net_generated.h comes from the concrete flatc custom target, not the fbs_schemas
# INTERFACE lib (see z13_module/CMakeLists.txt for why that distinction matters).
add_dependencies(${PROJECT_NAME}_objects GENERATE_FBS_SCHEMAS_HEADERS)

target_include_directories(
  ${PROJECT_NAME}_objects
  PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include/${PROJECT_NAMESPACE}-${PROJECT_VERSION}>
)

target_link_libraries(
  ${PROJECT_NAME}_objects
  PUBLIC
  zodiac13::settings
  PRIVATE
  zodiac13::core
  zodiac13::components
  zodiac13::primitives
  zodiac13::z13_module
  unofficial::enet::enet
  ${ENET_PLATFORM_LIBS}
  Boost::dll
)

add_library(${PROJECT_NAME} SHARED $<TARGET_OBJECTS:${PROJECT_NAME}_objects>)
add_library(${PROJECT_NAMESPACE}::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

# The tests link the plugin directly and use its internal symbols; MSVC exports nothing by default.
set_target_properties(${PROJECT_NAME} PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)

target_link_libraries(
  ${PROJECT_NAME}
  PUBLIC
  ${PROJECT_NAME}_objects
  zodiac13::settings
  PRIVATE
  zodiac13::core
  zodiac13::components
  zodiac13::primitives
  zodiac13::z13_module
  unofficial::enet::enet
  ${ENET_PLATFORM_LIBS}
  Boost::dll
)

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

set(MODULE_TARGET_PATH "modules/net_module")
set(MODULE_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/bin/${MODULE_TARGET_PATH}")

# Output layout matches z13_module/CMakeLists.txt and the path in z13_config.yaml.
set_target_properties(${PROJECT_NAME} PROPERTIES
    PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY ${MODULE_OUTPUT_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${MODULE_OUTPUT_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${MODULE_OUTPUT_DIR}
    LIBRARY_OUTPUT_DIRECTORY ${MODULE_OUTPUT_DIR}
    LIBRARY_OUTPUT_DIRECTORY_DEBUG ${MODULE_OUTPUT_DIR}
    LIBRARY_OUTPUT_DIRECTORY_RELEASE ${MODULE_OUTPUT_DIR}
)
