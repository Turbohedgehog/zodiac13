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

#include <Eigen/Dense>

namespace z13::gameplay {

// Persistent yaw/pitch for mouse-look, kept separate from the transform matrix.
// Re-deriving these via eulerAngles() from the matrix every frame let float error
// near +/-90 deg pitch (where the Z/X decomposition is ill-conditioned) leak into
// an unintended roll, which showed up as the skybox/scene tilting on vertical look.
struct LookAngles {
  float yaw_deg {};
  float pitch_deg {};
};

// Move-axis inputs for ApplyCameraMove, already resolved to plain floats from the
// action system's per-frame values so this function has no ECS/InputConfig
// dependency and is unit-testable on its own.
struct CameraMoveAxes {
  float forward {};
  float backward {};
  float right {};
  float left {};
  float up {};
  float down {};
  float yaw_delta_deg {};
  float pitch_delta_deg {};
};

constexpr float kCameraVelocity = 30.f;
constexpr float kMaxPitchDeg = 89.f;

// Applies one frame's mouse-look delta and move axes to `look` and `transform`.
// `transform`'s rotation block is fully overwritten from `look`; its translation
// column is read as the current position and then updated in place.
void ApplyCameraMove(
    const CameraMoveAxes& axes,
    float delta_time,
    LookAngles& look,
    Eigen::Matrix4f& transform);

}  // namespace z13::gameplay
