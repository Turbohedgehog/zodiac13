cmake_minimum_required(VERSION 3.13)

project(core)

set(CMAKE_CXX_STANDARD 23)

find_package(spdlog CONFIG REQUIRED)
find_package(Boost REQUIRED COMPONENTS program_options dll)
find_package(flecs CONFIG REQUIRED)
find_package(reflectcpp CONFIG REQUIRED)
find_package(Eigen3 REQUIRED NO_MODULE)
find_package(flatbuffers CONFIG REQUIRED)

find_path(FLATBUFFERS_CMAKE_DIR BuildFlatBuffers.cmake
          HINTS ${flatbuffers_DIR} "${flatbuffers_DIR}/../../../share/flatbuffers")
include(${FLATBUFFERS_CMAKE_DIR}/BuildFlatBuffers.cmake)

# lib_core owns the schema of its own settings and knows nothing of the game's schemas.
set(
  CORE_FBS_SCHEMAS
  ${PROJECT_SOURCE_DIR}/fbs/attributes.fbs
  ${PROJECT_SOURCE_DIR}/fbs/core_settings.fbs
  ${PROJECT_SOURCE_DIR}/fbs/world_snapshot.fbs
)
# The game schemas include these (see lib_z13/schemas/CMakeLists.txt).
set(CORE_FBS_SCHEMAS ${CORE_FBS_SCHEMAS} PARENT_SCOPE)

flatbuffers_generate_headers(
    TARGET CORE_SETTINGS_HEADERS
    SCHEMAS ${CORE_FBS_SCHEMAS}
    # Same flags as the game schemas: their generated headers reference this one's type table.
    FLAGS "--scoped-enums" "--reflect-names" "--gen-name-strings" "--gen-object-api" "--gen-compare" "--cpp-std" "c++17"
)

set(
  PROJECT_SOURCE_FILES
  src/settings/config.cpp
  src/settings/schema_attributes.cpp
  src/state/component_codec.cpp
  src/state/component_codec/meta_access.cpp
  src/state/component_codec/scratch_value.cpp
  src/state/component_codec/value_decoder.cpp
  src/state/component_codec/value_encoder.cpp
  src/state/component_meta.cpp
  src/state/group_bucket.cpp
  src/state/rollback.cpp
  src/state/snapshot_grouping.cpp
  src/state/world_json_store.cpp
  src/state/world_serializer.cpp
  src/state/world_snapshot_history.cpp
  src/state/world_state.cpp
  src/state/world_state_requests.cpp
  src/time/simulation_clock.cpp
  src/time/tick_pacer.cpp
  src/utils/camera_pose.cpp
  src/utils/drawn_poses.cpp
  src/utils/endpoint.cpp
  src/utils/file_io.cpp
  src/utils/flecs_utils.cpp
  src/utils/frustum.cpp
  src/utils/pose_smoothing.cpp
  src/utils/system_times.cpp
  src/world/core.cpp
  src/world/lifecycle.cpp
  src/world/module_lib_holder.cpp
  src/world/phase_order.cpp
)

# Shared: one copy for the executable and every plugin, instead of one inside each.
# Next to the executable, where Windows looks for a plugin's dependencies too.
add_library(${PROJECT_NAME} SHARED ${PROJECT_SOURCE_FILES})
set_target_properties(${PROJECT_NAME} PROPERTIES
    OUTPUT_NAME z13_core
    WINDOWS_EXPORT_ALL_SYMBOLS ON
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/bin
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
    LIBRARY_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/bin
    LIBRARY_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/bin
)

add_dependencies(${PROJECT_NAME} GENERATE_CORE_SETTINGS_HEADERS)
add_library(${PROJECT_NAMESPACE}::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/include/lib_core/version.h.in
  ${CMAKE_CURRENT_BINARY_DIR}/include/lib_core/version.h
  @ONLY
)

target_include_directories(
  ${PROJECT_NAME}
  PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/CORE_SETTINGS_HEADERS>
  $<INSTALL_INTERFACE:include/${PROJECT_NAMESPACE}-${PROJECT_VERSION}>
  $<BUILD_INTERFACE:${Boost_INCLUDE_DIR}>
)

target_link_libraries(
  ${PROJECT_NAME}
  PRIVATE
  Boost::dll
)

target_link_libraries(
  ${PROJECT_NAME}
  PUBLIC
  # config.h holds program_options members and CameraPose, so its users link them too.
  Boost::program_options
  Eigen3::Eigen
  flatbuffers::flatbuffers
  flecs::flecs_static
  spdlog::spdlog
  reflectcpp::reflectcpp
)

# set_target_properties(${PROJECT_NAME} PROPERTIES 
#     MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
# )

install(DIRECTORY include/ DESTINATION include/${PROJECT_NAMESPACE}-${PROJECT_VERSION}/)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/include/lib_core/version.h
        DESTINATION include/${PROJECT_NAMESPACE}-${PROJECT_VERSION}/lib_core)

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
