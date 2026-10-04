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

#include "z13_module/gameplay/camera_look.h"

#include <algorithm>
#include <cmath>

#include <lib_core/utils/math.h>

namespace z13::gameplay {

namespace {

constexpr float kHalfTurnDeg = 180.f;
constexpr float kFullTurnDeg = 360.f;

// Wraps yaw rather than clamping it, so turning past +-180 deg works; an in-range angle is
// left untouched, so a canonical value stays bit-exact.
LookAngles Normalized(LookAngles look) {
  if (look.yaw_deg <= -kHalfTurnDeg || look.yaw_deg > kHalfTurnDeg) {
    look.yaw_deg = std::fmod(look.yaw_deg + kHalfTurnDeg, kFullTurnDeg);
    if (look.yaw_deg < 0.f) {
      look.yaw_deg += kFullTurnDeg;
    }
    look.yaw_deg -= kHalfTurnDeg;
  }
  look.pitch_deg = std::clamp(look.pitch_deg, -kMaxPitchDeg, kMaxPitchDeg);
  return look;
}

}  // namespace

LookAngles TurnLook(LookAngles look, float yaw_delta_deg, float pitch_delta_deg) {
  look.yaw_deg += yaw_delta_deg;
  look.pitch_deg -= pitch_delta_deg;
  return Normalized(look);
}

LookAngles LookAnglesFromTransform(const Eigen::Matrix4f& transform) {
  const Eigen::Vector3f forward = transform.block<3, 3>(0, 0).col(0);
  return {
      .yaw_deg = z13::math::ToDegrees(std::atan2(forward.y(), forward.x())),
      .pitch_deg = z13::math::ToDegrees(-std::asin(std::clamp(forward.z(), -1.f, 1.f))),
  };
}

void ApplyCameraMove(
    const CameraMoveAxes& axes,
    float delta_time,
    LookAngles& look,
    Eigen::Matrix4f& transform) {
  look = Normalized(axes.absolute_look);

  auto rotation =
      Eigen::AngleAxisf(z13::math::ToRadians(look.yaw_deg), Eigen::Vector3f::UnitZ()) *
      Eigen::AngleAxisf(z13::math::ToRadians(look.pitch_deg), Eigen::Vector3f::UnitY());

  auto new_rotation_matrix = rotation.toRotationMatrix();
  transform.block<3, 3>(0, 0) = new_rotation_matrix;

  auto forward_axis = new_rotation_matrix.col(0);
  auto side_axis = new_rotation_matrix.col(1);
  auto up_axis = new_rotation_matrix.col(2);

  auto x_delta = forward_axis * (axes.forward - axes.backward) * delta_time * kCameraVelocity;
  auto y_delta = side_axis * (axes.left - axes.right) * delta_time * kCameraVelocity;
  auto z_delta = up_axis * (axes.up - axes.down) * delta_time * kCameraVelocity;

  Eigen::Vector3f position = transform.col(3).head<3>();
  position += x_delta + y_delta + z_delta;
  transform.block<3, 1>(0, 3) = position;
}

}  // namespace z13::gameplay
