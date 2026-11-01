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
#include <numbers>

#include <lib_core/utils/math.h>

namespace z13::gameplay {

namespace {

constexpr float kHalfTurnDeg = 180.f;
// UpFrame turns by the shortest arc, which there is none of for an up opposite +Z: one whose cosine with +Z is within
// this of -1 gets the half turn about X instead, so the division by 1 + cosine never meets zero.
constexpr float kOppositeCosineMargin = 1e-6f;
// A mix of two unit vectors whose squared length is this short is taken for their cancelling out.
constexpr float kCancelledSquaredLength = 1e-6f;
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

Eigen::Matrix3f UpFrame(const Eigen::Vector3f& up) {
  const float cosine = up.z();
  if (cosine <= -1.f + kOppositeCosineMargin) {
    return Eigen::AngleAxisf(std::numbers::pi_v<float>, Eigen::Vector3f::UnitX()).toRotationMatrix();
  }
  const Eigen::Vector3f axis = Eigen::Vector3f::UnitZ().cross(up);
  Eigen::Matrix3f cross;
  cross << 0.f, -axis.z(), axis.y(), axis.z(), 0.f, -axis.x(), -axis.y(), axis.x(), 0.f;
  return Eigen::Matrix3f::Identity() + cross + (cross * cross) / (1.f + cosine);
}

Eigen::Vector3f FollowUp(const Eigen::Vector3f& up, const Eigen::Vector3f& target, float follow) {
  const Eigen::Vector3f mixed = up + ((target - up) * std::clamp(follow, 0.f, 1.f));
  // Opposite vectors cancel: there is no way to turn that is nearer, so it goes straight to the target.
  return mixed.squaredNorm() > kCancelledSquaredLength ? Eigen::Vector3f(mixed.normalized()) : target;
}

LookAngles LookAnglesFromTransform(const Eigen::Matrix4f& transform, const Eigen::Vector3f& upright) {
  const Eigen::Vector3f forward = UpFrame(upright).transpose() * transform.block<3, 3>(0, 0).col(0);
  return {
      .yaw_deg = z13::math::ToDegrees(std::atan2(forward.y(), forward.x())),
      .pitch_deg = z13::math::ToDegrees(-std::asin(std::clamp(forward.z(), -1.f, 1.f))),
  };
}

namespace {

Eigen::Matrix3f Turn(const CameraMoveAxes& axes, LookAngles& look, Eigen::Matrix4f& transform) {
  look = Normalized(axes.absolute_look);
  const Eigen::Matrix3f rotation =
      UpFrame(axes.upright) *
      (Eigen::AngleAxisf(z13::math::ToRadians(look.yaw_deg), Eigen::Vector3f::UnitZ()) *
       Eigen::AngleAxisf(z13::math::ToRadians(look.pitch_deg), Eigen::Vector3f::UnitY()))
          .toRotationMatrix();
  transform.block<3, 3>(0, 0) = rotation;
  return rotation;
}

}  // namespace

void ApplyCameraMove(
    const CameraMoveAxes& axes,
    float delta_time,
    LookAngles& look,
    Eigen::Matrix4f& transform) {
  const Eigen::Matrix3f new_rotation_matrix = Turn(axes, look, transform);

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

void ApplyWalkMove(
    const CameraMoveAxes& axes, const WalkStep& step, LookAngles& look, PlayerMotion& motion,
    Eigen::Matrix4f& transform) {
  const Eigen::Matrix3f rotation = Turn(axes, look, transform);
  const Eigen::Vector3f down = step.gravity.normalized();
  // The camera stays upright along its up; its heading is laid onto the floor across the gravity.
  const Eigen::Vector3f facing = rotation.col(0) - (rotation.col(0).dot(down) * down);
  const Eigen::Vector3f forward = facing.isZero() ? Eigen::Vector3f::Zero() : facing.normalized();
  const Eigen::Vector3f left = forward.cross(down);
  Eigen::Vector3f heading = forward * (axes.forward - axes.backward) + left * (axes.left - axes.right);
  // Diagonal keys don't walk faster than one.
  if (heading.squaredNorm() > 1.f) {
    heading.normalize();
  }

  if (motion.grounded && axes.up > 0.f) {
    motion = {.fall_speed = -step.jump_speed};
  }
  motion.fall_speed += step.gravity.norm() * step.delta_time;

  transform.block<3, 1>(0, 3) +=
      ((heading * step.walk_speed) + (down * motion.fall_speed)) * step.delta_time;
}

}  // namespace z13::gameplay
