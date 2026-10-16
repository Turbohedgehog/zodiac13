project(net_module)

find_package(unofficial-enet CONFIG REQUIRED)
find_package(libuv CONFIG REQUIRED)
find_package(Boost REQUIRED COMPONENTS dll)

# vcpkg's enet links these itself only for MinGW.
if(WIN32)
  set(ENET_PLATFORM_LIBS winmm ws2_32)
endif()

z13_add_plugin(
  ${PROJECT_NAME} modules/net_module FBS_HEADERS EXPORT_ALL_SYMBOLS
  SOURCES
  src/catch_up.cpp
  src/client_session.cpp
  src/clock_sync.cpp
  src/discovery_messages.cpp
  src/divergence_dump.cpp
  src/discovery_socket.cpp
  src/enet_datagram.cpp
  src/enet_lifetime.cpp
  src/enet_transport.cpp
  src/in_memory_transport.cpp
  src/lan_discovery_system.cpp
  src/net_action_sender.cpp
  src/net_module.cpp
  src/net_module_factory.cpp
  src/net_session.cpp
  src/net_session_system.cpp
  src/network_interfaces.cpp
  src/protocol.cpp
  src/remote_input.cpp
  src/reply_limiter.cpp
  src/scheduled_commands.cpp
  src/server_finder.cpp
  src/server_session.cpp
  src/session_control.cpp
  src/session_deltas.cpp
  src/session_settings.cpp
  src/state_digest.cpp
  src/wire_records.cpp
  PUBLIC
  zodiac13::settings
  PRIVATE
  zodiac13::core
  zodiac13::components
  zodiac13::primitives
  zodiac13::z13_module
  unofficial::enet::enet
  $<IF:$<TARGET_EXISTS:libuv::uv_a>,libuv::uv_a,libuv::uv>
  ${ENET_PLATFORM_LIBS}
  Boost::dll
)
