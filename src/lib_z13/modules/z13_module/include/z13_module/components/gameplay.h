#pragma once

#include <z13_module/components/geometry.h>

namespace z13::gameplay {

struct Gameplay {
};

struct Pause {};

struct Camera {
  z13::geometry::Vector3 position = z13::geometry::Vector3::kZero;
  z13::geometry::Quaternion orientation = z13::geometry::Quaternion::kIdentity;
};

}  // namespace z13::gameplay
