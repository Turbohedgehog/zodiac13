#pragma once

#include <lib_core/log.h>

namespace z13::input {

struct SystemInputEvent {};

struct MousePos {
  ~MousePos() {
    // LOG_INFO("~~~ ~MousePos: {}", idx);
  }
  int x;
  int y;
  // int idx = -1;

  static constinit MousePos kZero;
};

inline constinit MousePos MousePos::kZero = {0, 0};

struct MouseMoveEvent {
  MousePos delta;
};

enum class MouseButton {
  kLeft,
  kMiddle,
  kRight,
};

struct MouseButtonEvent {
  MousePos pos;
  MouseButton mouse_button;
  uint8_t clicks;
};

struct MouseButtonDownEvent : public MouseButtonEvent {
};

struct MouseButtonUpEvent : MouseButtonEvent {
};

struct Key {
  int32_t code;
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

}  // namespace z13::input
