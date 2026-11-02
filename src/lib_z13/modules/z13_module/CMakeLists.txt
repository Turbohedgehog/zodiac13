project(z13_module)

find_package(Eigen3 REQUIRED NO_MODULE)
find_package(Boost REQUIRED COMPONENTS dll)

# The tests link this one.
z13_add_plugin(
  ${PROJECT_NAME} modules/z13_module FBS_HEADERS EXPORT_ALL_SYMBOLS
  PRECOMPILE "<flecs.h>" "<Eigen/Dense>"
  SOURCES
  src/bootstrap/bootstrap_system.cpp
  src/gameplay/block_change_system.cpp
  src/gameplay/flashlight_system.cpp
  src/gameplay/gameplay_system.cpp
  src/gameplay/inventory_system.cpp
  src/gameplay/player_roster_log.cpp
  src/input/gameplay_input_system.cpp
  src/input/input_script.cpp
  src/input/scripted_input_system.cpp
  src/input/touch_input_system.cpp
  src/state/player_action_recorder.cpp
  src/state/quick_save_input_system.cpp
  src/state/replay.cpp
  src/z13_module.cpp
  src/z13_module_factory.cpp
  PUBLIC
  # player/camera_look.h (a public header) uses Eigen::Matrix4f.
  Eigen3::Eigen
  zodiac13::player
  PRIVATE
  zodiac13::core
  zodiac13::components
  zodiac13::settings
  zodiac13::touch
  zodiac13::input
  Boost::dll
)
