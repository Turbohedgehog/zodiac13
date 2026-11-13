project(console_input)

# Stateless, like touch: parses a console line into the arguments of a command.
z13_add_library(
  ${PROJECT_NAME} STATIC
  SOURCES
  src/console_input.cpp
  src/console_session.cpp
  PUBLIC
  zodiac13::core
)
