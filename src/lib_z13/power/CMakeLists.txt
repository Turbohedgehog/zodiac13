project(power)

# Stateless, like networks: each plugin linking its own copy shares nothing.
z13_add_library(
  ${PROJECT_NAME} STATIC OPTIMIZE_IN_DEBUG
  SOURCES
  src/balance.cpp
  src/port_names.cpp
  PUBLIC
  zodiac13::primitives
)
