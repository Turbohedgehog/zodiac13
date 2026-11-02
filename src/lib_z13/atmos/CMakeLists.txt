project(atmos)

# Stateless, like networks and power: each plugin linking its own copy shares nothing.
z13_add_library(
  ${PROJECT_NAME} STATIC OPTIMIZE_IN_DEBUG
  SOURCES
  src/air.cpp
  src/devices.cpp
  src/exchange.cpp
  src/port_names.cpp
  PUBLIC
  zodiac13::primitives
)
