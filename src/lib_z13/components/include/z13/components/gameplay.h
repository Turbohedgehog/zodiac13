#pragma once

#include <string>

namespace z13::gameplay {

struct PreUpdatePhase {};
struct UpdatePhase {};
struct PostUpdatePhase {};

struct Gameplay {
  uint32_t last_registered_player_id = 0;
};

struct Pause {};

struct WindowFocusEvent {
  bool has_focus = false;
};

struct WindowBackEvent {};

struct Player {
  uint32_t id = 0;
};

struct Camera {
  float fov = 90.f;
  std::string name;
};

}  // namespace z13::gameplay
