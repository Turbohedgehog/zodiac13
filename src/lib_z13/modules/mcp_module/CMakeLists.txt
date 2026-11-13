project(mcp_module)

find_package(Boost REQUIRED COMPONENTS dll)
find_package(Threads REQUIRED)

z13_add_plugin(
  ${PROJECT_NAME} modules/mcp_module EXPORT_ALL_SYMBOLS
  PRECOMPILE "<flecs.h>"
  SOURCES
  src/mcp_commands.cpp
  src/mcp_filter.cpp
  src/mcp_module.cpp
  src/mcp_module_factory.cpp
  src/mcp_protocol.cpp
  src/mcp_server.cpp
  src/mcp_server_state.cpp
  src/mcp_session.cpp
  src/mcp_system.cpp
  PRIVATE
  zodiac13::core
  zodiac13::components
  zodiac13::settings
  Boost::dll
  Threads::Threads
)
