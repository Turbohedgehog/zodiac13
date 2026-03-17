#pragma once

#include <string>

namespace z13::gameplay {

struct Gameplay {};

struct Pause {};

struct WindowFocusEvent {
  bool has_focus = false;
};

struct WindowBackEvent {};

struct Camera {
  float fov = 90.f;
  std::string name;
};

}  // namespace z13::gameplay
