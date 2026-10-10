cmake_minimum_required(VERSION 3.21)

project(z13_tests)

set(CMAKE_CXX_STANDARD 23)

find_package(GTest CONFIG REQUIRED)
find_package(reflectcpp CONFIG REQUIRED)

include(GenerateExportHeader)
include(GoogleTest)

# Test code lives either here (for cross-module/serialization tests) or next to
# the module it covers, under that module's own tests/ directory.
set(MODULES_DIR ${CMAKE_SOURCE_DIR}/src/lib_z13/modules)
set(
  PROJECT_SOURCE_FILES
  src/lib_core/config_test.cpp
  src/lib_core/core_settings_test.cpp
  src/lib_core/drawn_poses_test.cpp
  src/lib_core/endpoint_test.cpp
  src/lib_core/enum_cycle_test.cpp
  src/lib_core/file_io_test.cpp
  src/lib_core/flecs_state/component_codec_test.cpp
  src/lib_core/flecs_state/eigen_meta_test.cpp
  src/lib_core/flecs_state/flecs_state_roundtrip_test.cpp
  src/lib_core/flecs_state/json_canonical.cpp
  src/lib_core/flecs_state/serialization_efficiency_test.cpp
  src/lib_core/flecs_state/snapshot_grouping_test.cpp
  src/lib_core/flecs_state/test_components.cpp
  src/lib_core/flecs_state/world_fixture.cpp
  src/lib_core/flecs_state/world_state_requests_test.cpp
  src/lib_core/flecs_state/world_state_test.cpp
  src/lib_core/frame_start_test.cpp
  src/lib_core/frustum_test.cpp
  src/lib_core/lifecycle_test.cpp
  src/lib_core/pose_smoothing_test.cpp
  src/lib_core/schema_attributes_test.cpp
  src/lib_core/tick_pacer_test.cpp
  src/lib_z13/grid/block_index_test.cpp
  src/lib_z13/primitives/chunk_mesh_test.cpp
  src/lib_z13/primitives/cut_test.cpp
  src/lib_z13/primitives/draw_order_test.cpp
  src/lib_z13/primitives/geometry_test.cpp
  src/lib_z13/primitives/palette_test.cpp
  src/lib_z13/primitives/placement_test.cpp
  src/lib_z13/rooms/disjoint_sets_test.cpp
  src/lib_z13/rooms/room_builder_test.cpp
  src/lib_z13/rooms/room_cache_test.cpp
  src/lib_z13/rooms/room_visibility_test.cpp
  src/lib_z13/rooms/tour_viewpoints_test.cpp
  src/test_runner.cpp
  src/z13_launcher/module_list_test.cpp
  src/z13_launcher/settings_loader_test.cpp
  src/z13_launcher/station_assets_check_test.cpp
  ${MODULES_DIR}/building_module/tests/block_building_test.cpp
  ${MODULES_DIR}/building_module/tests/block_snapshot_test.cpp
  ${MODULES_DIR}/building_module/tests/brush_aim_test.cpp
  ${MODULES_DIR}/building_module/tests/brush_test.cpp
  ${MODULES_DIR}/building_module/tests/build_validation_test.cpp
  ${MODULES_DIR}/building_module/tests/building_block_test.cpp
  ${MODULES_DIR}/building_module/tests/building_mode_test.cpp
  ${MODULES_DIR}/building_module/tests/palette_system_test.cpp
  ${MODULES_DIR}/building_module/tests/player_order_test.cpp
  ${MODULES_DIR}/building_module/tests/primitive_build_test.cpp
  ${MODULES_DIR}/building_module/tests/station_mode_test.cpp
  ${MODULES_DIR}/building_module/tests/test_station_test.cpp
  ${MODULES_DIR}/bullet_module/tests/physics_collision_test.cpp
  ${MODULES_DIR}/net_module/tests/protocol/codec_test.cpp
  ${MODULES_DIR}/net_module/tests/transport/enet_transport_test.cpp
  ${MODULES_DIR}/net_module/tests/transport/in_memory_transport_test.cpp
  ${MODULES_DIR}/station_module/tests/room_system_test.cpp
  ${MODULES_DIR}/z13_module/tests/gameplay/camera_look_test.cpp
  ${MODULES_DIR}/z13_module/tests/gameplay/main_menu_test.cpp
  ${MODULES_DIR}/z13_module/tests/gameplay/pause_test.cpp
  ${MODULES_DIR}/z13_module/tests/gameplay/player_color_test.cpp
  ${MODULES_DIR}/z13_module/tests/gameplay/server_launch_test.cpp
  ${MODULES_DIR}/z13_module/tests/gameplay/spawn_point_test.cpp
  ${MODULES_DIR}/z13_module/tests/input/action_negotiation_test.cpp
  ${MODULES_DIR}/z13_module/tests/input/input_config_defaults_test.cpp
  ${MODULES_DIR}/z13_module/tests/input/input_pipeline_test.cpp
  ${MODULES_DIR}/z13_module/tests/net/action_negotiation_net_test.cpp
  ${MODULES_DIR}/z13_module/tests/net/clock_sync_test.cpp
  ${MODULES_DIR}/z13_module/tests/net/command_stream_test.cpp
  ${MODULES_DIR}/z13_module/tests/net/desync_test.cpp
  ${MODULES_DIR}/z13_module/tests/net/listen_server_test.cpp
  ${MODULES_DIR}/z13_module/tests/net/motion_metrics_test.cpp
  ${MODULES_DIR}/z13_module/tests/net/net_session_test.cpp
  ${MODULES_DIR}/z13_module/tests/net/paused_peer_test.cpp
  ${MODULES_DIR}/z13_module/tests/net/quick_save_in_session_test.cpp
  ${MODULES_DIR}/z13_module/tests/net/remote_prediction_test.cpp
  ${MODULES_DIR}/z13_module/tests/net/session_settings_test.cpp
  ${MODULES_DIR}/z13_module/tests/net/uplink_budget_test.cpp
  ${MODULES_DIR}/z13_module/tests/state/action_quantization_test.cpp
  ${MODULES_DIR}/z13_module/tests/state/action_replay_test.cpp
  ${MODULES_DIR}/z13_module/tests/state/cross_platform_state_test.cpp
  ${MODULES_DIR}/z13_module/tests/state/game_state_test.cpp
  ${MODULES_DIR}/z13_module/tests/state/phase_order_test.cpp
  ${MODULES_DIR}/z13_module/tests/state/player_action_log_test.cpp
  ${MODULES_DIR}/z13_module/tests/state/quick_save_test.cpp
  ${MODULES_DIR}/z13_module/tests/state/two_player_replay_test.cpp
  ${MODULES_DIR}/z13_module/tests/state/world_snapshot_history_test.cpp
  ${MODULES_DIR}/z13_module/tests/support/motion_metrics.cpp
)

if(Z13_BUILD_BENCHMARKS)
  list(
    APPEND
    PROJECT_SOURCE_FILES
    ${MODULES_DIR}/station_module/tests/bench/room_build_bench_test.cpp
    ${MODULES_DIR}/z13_module/tests/bench/render_chunk_bench_test.cpp
    ${MODULES_DIR}/z13_module/tests/bench/station_load_bench_test.cpp
    ${MODULES_DIR}/z13_module/tests/bench/sync_bench_test.cpp
  )
endif()

# Standalone DLL exercising lib_core and lib_z13.
add_library(${PROJECT_NAME} SHARED ${PROJECT_SOURCE_FILES})
add_library(${PROJECT_NAMESPACE}::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

# Third-party test headers (reflect-cpp, gtest, flecs meta) are noisy under /W4.
set_target_properties(${PROJECT_NAME} PROPERTIES COMPILE_WARNING_AS_ERROR OFF)

generate_export_header(${PROJECT_NAME}
  BASE_NAME Z13_TESTS
  EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/z13_tests/z13_tests_export.h
)

target_include_directories(
  ${PROJECT_NAME}
  PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
)

target_link_libraries(
  ${PROJECT_NAME}
  PRIVATE
  zodiac13::core
  zodiac13::components
  zodiac13::z13_module
  zodiac13::building_module
  zodiac13::bullet_module
  zodiac13::station_module
  zodiac13::net_module
  zodiac13::z13_launcher
  zodiac13::z13_settings
  zodiac13::primitives
  zodiac13::z13_grid
  zodiac13::rooms
  GTest::gtest
  GTest::gmock
  reflectcpp::reflectcpp
)

# The palette and blueprint tests read the shipped assets from the source tree.
target_compile_definitions(${PROJECT_NAME} PRIVATE Z13_SOURCE_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets")

# Windows finds the plugin DLLs only next to the exe; ELF finds them by build-tree RPATH.
if(WIN32)
  add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      $<TARGET_FILE:zodiac13::core>
      $<TARGET_FILE:zodiac13::z13_module>
      $<TARGET_FILE:zodiac13::building_module>
      $<TARGET_FILE:zodiac13::bullet_module>
      $<TARGET_FILE:zodiac13::station_module>
      $<TARGET_FILE:zodiac13::net_module>
      $<TARGET_FILE_DIR:${PROJECT_NAME}>
  )
endif()

SET(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "${OUTPUT_DIRECTORY}")
SET(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${OUTPUT_DIRECTORY}")

set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/tests
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/bin/tests
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/bin/tests
)

# Host executable CTest invokes; the test code stays in the DLL.
add_executable(z13_test_runner runner/main.cpp)
target_link_libraries(z13_test_runner PRIVATE ${PROJECT_NAME} GTest::gtest)
set_target_properties(z13_test_runner PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/tests
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/bin/tests
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/bin/tests
)

gtest_discover_tests(z13_test_runner
    DISCOVERY_MODE PRE_TEST
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/tests
)

# The launcher's plugin path, out of process: z13_test_runner links the plugins directly
# instead, so it can't tell whether separately loaded plugins share one flecs.
find_package(Boost REQUIRED COMPONENTS dll)
add_executable(z13_plugin_smoke plugin_smoke/main.cpp)
target_link_libraries(z13_plugin_smoke PRIVATE zodiac13::core zodiac13::z13_launcher Boost::dll)
# Only for test_dll_messages.h; the plugin itself is loaded, not linked.
target_include_directories(z13_plugin_smoke PRIVATE ${CMAKE_SOURCE_DIR}/src/lib_test_dll/include)
set_target_properties(z13_plugin_smoke PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/tests
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/bin/tests
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/bin/tests
)
if(WIN32)
  add_custom_command(TARGET z13_plugin_smoke POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:zodiac13::core> $<TARGET_FILE_DIR:z13_plugin_smoke>
  )
endif()
add_dependencies(z13_plugin_smoke z13_config z13_module building_module net_module bullet_module station_module test_dll_module)
add_test(NAME PluginSmoke COMMAND z13_plugin_smoke)
