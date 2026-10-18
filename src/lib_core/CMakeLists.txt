project(core)

find_package(spdlog CONFIG REQUIRED)
find_package(Boost REQUIRED COMPONENTS program_options dll)
find_package(flecs CONFIG REQUIRED)
find_package(reflectcpp CONFIG REQUIRED)
find_package(Eigen3 REQUIRED NO_MODULE)
find_package(flatbuffers CONFIG REQUIRED)
find_package(Taskflow CONFIG REQUIRED)

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

# Shared: one copy for the executable and every plugin, instead of one inside each.
z13_add_library(
  ${PROJECT_NAME} SHARED EXPORT_ALL_SYMBOLS
  PRECOMPILE "<flecs.h>"
  SOURCES
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
  src/state/snapshot_hash.cpp
  src/state/world_json_store.cpp
  src/state/world_serializer.cpp
  src/state/world_snapshot_history.cpp
  src/state/world_state.cpp
  src/state/world_state_requests.cpp
  src/time/simulation_clock.cpp
  src/time/tick_log.cpp
  src/time/tick_pacer.cpp
  src/utils/camera_pose.cpp
  src/utils/drawn_poses.cpp
  src/utils/endpoint.cpp
  src/utils/file_io.cpp
  src/utils/flecs_utils.cpp
  src/utils/frustum.cpp
  src/utils/asset_source.cpp
  src/utils/install_dir.cpp
  src/utils/pose_smoothing.cpp
  src/utils/system_times.cpp
  src/utils/thread_pool.cpp
  src/world/core.cpp
  src/world/lifecycle.cpp
  src/world/module_lib_holder.cpp
  src/world/phase_order.cpp
  PUBLIC
  # config.h holds program_options members and CameraPose, so its users link them too.
  Boost::program_options
  Eigen3::Eigen
  flatbuffers::flatbuffers
  flecs::flecs_static
  spdlog::spdlog
  zodiac13::z13_profile
  reflectcpp::reflectcpp
  PRIVATE
  Boost::dll
  Taskflow::Taskflow
)
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME z13_core)
# Next to the executable, where Windows looks for a plugin's dependencies too.
z13_set_output_directory(${PROJECT_NAME} ${Z13_BIN_DIR})
add_dependencies(${PROJECT_NAME} GENERATE_CORE_SETTINGS_HEADERS)

configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/include/lib_core/version.h.in
  ${CMAKE_CURRENT_BINARY_DIR}/include/lib_core/version.h
  @ONLY
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/include/lib_core/version.h
        DESTINATION ${Z13_INSTALL_INCLUDE_DIR}/lib_core)

target_include_directories(
  ${PROJECT_NAME}
  PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/CORE_SETTINGS_HEADERS>
  $<BUILD_INTERFACE:${Boost_INCLUDE_DIR}>
)
