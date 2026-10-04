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

// Persistent yaw/pitch for mouse-look: re-deriving via eulerAngles() every frame
// let float error near +/-90 deg pitch leak into an unintended roll.
struct LookAngles {
  using State = void;
  float yaw_deg {};
  float pitch_deg {};
};

// Move-axis inputs for ApplyCameraMove, already resolved to plain floats so this
// function has no ECS/InputConfig dependency and is unit-testable on its own.
struct CameraMoveAxes {
  float forward {};
  float backward {};
  float right {};
  float left {};
  float up {};
  float down {};
  // Absolute angles: look actions carry where the player looks, not how far it turned.
  LookAngles look;
};

constexpr float kCameraVelocity = 30.f;
constexpr float kMaxPitchDeg = 89.f;

// Turns `look` by a mouse-look delta: yaw wraps into (-180:180], pitch clamps.
LookAngles TurnLook(LookAngles look, float yaw_delta_deg, float pitch_delta_deg);

// The angles `transform` faces, for a player without LookAngles yet.
LookAngles LookAnglesFromTransform(const Eigen::Matrix4f& transform);

// Sets `look` to the axes' angles and applies the move axes to `transform`, scaled by
// `delta_time`. `transform`'s rotation block is fully overwritten from `look`.
void ApplyCameraMove(
    const CameraMoveAxes& axes,
    float delta_time,
    LookAngles& look,
    Eigen::Matrix4f& transform);

}  // namespace z13::gameplay
