project(console_input)

# Stateless, like touch: parses a console line into the arguments of a command.
z13_add_library(
  ${PROJECT_NAME} STATIC
  SOURCES
  src/console_completion.cpp
  src/console_input.cpp
  src/console_session.cpp
  src/console_variables.cpp
  src/line_completion.cpp
  PUBLIC
  zodiac13::core
  zodiac13::settings
)
