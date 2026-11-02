project(input)

# Stateless, like touch: the plugins that read the input config each link their own copy.
z13_add_library(
  ${PROJECT_NAME} STATIC FBS_HEADERS
  SOURCES
  src/action_negotiation.cpp
  src/input_config_loader.cpp
  PUBLIC
  zodiac13::components
  zodiac13::core
  zodiac13::fbs_schemas
  PRIVATE
  zodiac13::settings
)
