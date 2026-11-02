project(net_module)

find_package(unofficial-enet CONFIG REQUIRED)
find_package(libuv CONFIG REQUIRED)
find_package(Boost REQUIRED COMPONENTS dll)

# vcpkg's enet links these itself only for MinGW.
if(WIN32)
  set(ENET_PLATFORM_LIBS winmm ws2_32)
endif()

# The schemas only this module uses; they include the shared ones and lib_core's.
z13_generate_schemas(
  NET_MODULE_SCHEMAS
  SCHEMAS
  ${PROJECT_SOURCE_DIR}/fbs/discovery.fbs
  ${PROJECT_SOURCE_DIR}/fbs/net.fbs
  INCLUDE ${Z13_SHARED_SCHEMA_DIR} ${CMAKE_SOURCE_DIR}/src/lib_core/fbs
)

z13_add_plugin(
  ${PROJECT_NAME} modules/net_module FBS_HEADERS EXPORT_ALL_SYMBOLS
  PRECOMPILE "<flecs.h>"
  SOURCES
  src/session/action_hints.cpp
  src/session/catch_up.cpp
  src/session/client_session.cpp
  src/protocol/clock_sync.cpp
  src/discovery/discovery_messages.cpp
  src/discovery/discovery_socket.cpp
  src/session/divergence_dump.cpp
  src/transport/enet_datagram.cpp
  src/transport/enet_lifetime.cpp
  src/transport/enet_transport.cpp
  src/sectors/ghost_players.cpp
  src/sectors/ghost_poses.cpp
  src/sectors/held_sector_history.cpp
  src/protocol/hint_classes.cpp
  src/protocol/hint_packet.cpp
  src/transport/in_memory_transport.cpp
  src/discovery/lan_discovery_system.cpp
  src/session/net_action_sender.cpp
  src/sectors/interest_deltas.cpp
  src/sectors/chunk_reach.cpp
  src/sectors/interest_events.cpp
  src/sectors/interest_system.cpp
  src/sectors/interest_view.cpp
  src/session/light_client.cpp
  src/net_module.cpp
  src/net_module_factory.cpp
  src/session/net_session.cpp
  src/session/net_session_system.cpp
  src/protocol/pose_packet.cpp
  src/discovery/network_interfaces.cpp
  src/sectors/player_positions.cpp
  src/protocol/protocol.cpp
  src/session/remote_input.cpp
  src/protocol/reply_limiter.cpp
  src/session/scheduled_commands.cpp
  src/sectors/sector_hash.cpp
  src/sectors/sector_system.cpp
  src/transport/sent_traffic.cpp
  src/discovery/server_finder.cpp
  src/session/server_session.cpp
  src/session/session_control.cpp
  src/session/session_deltas.cpp
  src/session/session_settings.cpp
  src/sectors/simulated_set.cpp
  src/protocol/state_digest.cpp
  src/session/station_bounds_log.cpp
  src/protocol/wire_records.cpp
  src/session/world_edits.cpp
  PUBLIC
  zodiac13::settings
  PRIVATE
  zodiac13::core
  zodiac13::components
  zodiac13::primitives
  zodiac13::sectors
  zodiac13::input
  zodiac13::player
  unofficial::enet::enet
  $<IF:$<TARGET_EXISTS:libuv::uv_a>,libuv::uv_a,libuv::uv>
  ${ENET_PLATFORM_LIBS}
  Boost::dll
)

# net_generated.h is in protocol.h, so the plugin's users need the directory.
add_dependencies(${PROJECT_NAME} GENERATE_NET_MODULE_SCHEMAS)
target_include_directories(${PROJECT_NAME} PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/NET_MODULE_SCHEMAS>)
