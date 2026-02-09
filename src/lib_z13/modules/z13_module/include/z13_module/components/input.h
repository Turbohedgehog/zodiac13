#pragma once

#include <array>
#include <vector>
#include <map>

#include <input_config.pb.h>

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
  z13::proto::input::Keyboard::Code button;
  uint8_t clicks;
};

struct MouseButtonDownEvent : public MouseButtonEvent {
};

struct MouseButtonUpEvent : MouseButtonEvent {
};

struct Key {
  z13::proto::input::Keyboard::Code code;
  int32_t raw_code;
  int8_t mod;
  uint8_t repeat;
};

struct KeyboardEvent {
  Key key;
};

struct KeyboardDownEvent : KeyboardEvent {
};

struct KeyboardUpEvent : KeyboardEvent {
};

struct ActionBinding {
  z13::proto::input::Action::ActionType action;
  std::vector<z13::proto::input::Keyboard::Code> keys;
};

struct InputConfig {
  std::vector<ActionBinding> action_bindings;
  std::map<z13::proto::input::Keyboard::Code, z13::proto::input::Action::ActionType> code_to_action;
  float mouse_sensitivity = 5.f;
  bool invert_x = false;
  bool invert_y = false;
};

struct InputState {
  std::array<float, z13::proto::input::Keyboard_Code_Code_ARRAYSIZE> input_state = {0.f};
};

struct InputListener {};

struct CurrentActionListenerTag {};

struct ActionListener {
  std::array<float, z13::proto::input::Action_ActionType_ActionType_ARRAYSIZE> action_values = {0.f};
};

}  // namespace z13::input
