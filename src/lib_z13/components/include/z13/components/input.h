#pragma once

#include <array>
#include <vector>
#include <map>

#include <input_config_generated.h>

namespace z13::input {

struct SystemInputEvent {};

struct MousePos {
  int x;
  int y;

  static constinit MousePos kZero;
};

inline constinit MousePos MousePos::kZero = {0, 0};

struct MouseMoveEvent {
  MousePos delta;
};

struct MouseButtonEvent {
  MousePos pos;
  z13::fbs::input::Keycode button;
  uint8_t clicks;
};

struct MouseButtonDownEvent : public MouseButtonEvent {
};

struct MouseButtonUpEvent : MouseButtonEvent {
};

struct Keycode {
  z13::fbs::input::Keycode code;
  int32_t raw_code;
  int8_t mod;
  uint8_t repeat;
};

struct KeyboardEvent {
  Keycode keycode;
};

struct KeyboardDownEvent : KeyboardEvent {
};

struct KeyboardUpEvent : KeyboardEvent {
};

struct ActionBinding {
  z13::fbs::input::Action action;
  std::vector<z13::fbs::input::Keycode> keys;
};

struct InputConfig {
  std::vector<ActionBinding> action_bindings;
  std::map<z13::fbs::input::Keycode, z13::fbs::input::Action> code_to_action;
  float mouse_sensitivity = 5.f;
  bool invert_x = false;
  bool invert_y = false;
};

struct InputState {
  std::array<float, static_cast<size_t>(z13::fbs::input::Keycode::MAX) + 1> input_state = {0.f};
};

struct InputListener {};

struct CurrentActionListenerTag {};

struct ActionListener {
  std::array<float, static_cast<size_t>(z13::fbs::input::Action::MAX) + 1> action_values = {0.f};
};

}  // namespace z13::input
