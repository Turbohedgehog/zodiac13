#pragma once

#include <string>

namespace z13::gameplay {

struct Gameplay {
};

struct Pause {};

struct Camera {
  float fov = 90.f;
  std::string name;
  float h_rotation_deg = 0.f;
  float v_rotation_deg = 0.f;
};

}  // namespace z13::gameplay
