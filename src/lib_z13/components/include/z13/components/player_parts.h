/*
 * Copyright 2026 Ivan Kulenko / Zodiac13
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://apache.org
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <string>

#include <Eigen/Dense>

namespace z13::gameplay {

struct Camera {
  using State = void;
  float fov = 90.f;
  std::string name;
};

struct PlayerCollider {
  using State = void;
  float radius {};
};

struct Flashlight {
  using State = void;
  bool on {};
};

// A player under gravity (z13/components/gravity.h) walks; without it, flies freely.
struct PlayerMotion {
  using State = void;
  bool grounded {};
  // Along the gravity, negative while rising after a jump.
  float fall_speed {};
  // The speed a player without gravity keeps, in m/s (physics.drift_thrust).
  Eigen::Vector3f drift = Eigen::Vector3f::Zero();
};

}  // namespace z13::gameplay
