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

#include <lib_core/utils/pose_smoothing.h>

#include <lib_core/utils/math.h>

namespace z13::math {

namespace {

// Coefficients of the rational approximation of exp(-x) in Game Programming Gems 4, 1.10.
constexpr float kExpCoefficient2 = 0.48f;
constexpr float kExpCoefficient3 = 0.235f;
constexpr float kOmegaPerSmoothTime = 2.f;

// One step of a critically damped spring from `current` (moving at `velocity`) to `target`.
Eigen::Vector3f SpringStep(
    const Eigen::Vector3f& current, const Eigen::Vector3f& target, Eigen::Vector3f& velocity, float smooth_time,
    float delta_seconds) {
  const float omega = kOmegaPerSmoothTime / smooth_time;
  const float x = omega * delta_seconds;
  const float decay = 1.f / (1.f + x + kExpCoefficient2 * x * x + kExpCoefficient3 * x * x * x);
  const Eigen::Vector3f error = current - target;
  const Eigen::Vector3f pull = (velocity + omega * error) * delta_seconds;
  velocity = (velocity - omega * pull) * decay;
  Eigen::Vector3f next = target + (error + pull) * decay;
  if ((target - current).dot(next - target) > 0.f) {
    next = target;
    velocity.setZero();
  }
  return next;
}

Eigen::Quaternionf RotationOf(const Eigen::Matrix4f& transform) {
  return Eigen::Quaternionf(transform.block<3, 3>(0, 0)).normalized();
}

// The rotation from `from` to `to` as axis * angle, the short way round.
Eigen::Vector3f RotationVector(const Eigen::Quaternionf& from, const Eigen::Quaternionf& to) {
  Eigen::Quaternionf delta = to * from.conjugate();
  if (delta.w() < 0.f) {
    delta.coeffs() = -delta.coeffs();
  }
  const Eigen::AngleAxisf angle_axis(delta.normalized());
  return angle_axis.axis() * angle_axis.angle();
}

Eigen::Quaternionf FromRotationVector(const Eigen::Vector3f& rotation) {
  const float angle = rotation.norm();
  if (angle <= 0.f) {
    return Eigen::Quaternionf::Identity();
  }
  return Eigen::Quaternionf(Eigen::AngleAxisf(angle, rotation / angle));
}

}  // namespace

SmoothedPose PoseAt(const Eigen::Matrix4f& transform) {
  return {.position = ExtractTranslation(transform), .rotation = RotationOf(transform)};
}

void ChasePose(SmoothedPose& pose, const Eigen::Matrix4f& target, float delta_seconds, const PoseSmoothingParams& params) {
  const Eigen::Vector3f target_position = ExtractTranslation(target);
  const Eigen::Quaternionf target_rotation = RotationOf(target);
  const Eigen::Vector3f rotation_error = RotationVector(pose.rotation, target_rotation);
  if (params.smooth_time_seconds <= 0.f || delta_seconds <= 0.f ||
      (target_position - pose.position).norm() > params.snap_distance ||
      rotation_error.norm() > params.snap_angle_rad) {
    pose = PoseAt(target);
    return;
  }

  pose.position = SpringStep(pose.position, target_position, pose.velocity, params.smooth_time_seconds, delta_seconds);
  const Eigen::Vector3f turn = SpringStep(
      Eigen::Vector3f::Zero(), rotation_error, pose.angular_velocity, params.smooth_time_seconds, delta_seconds);
  pose.rotation = (FromRotationVector(turn) * pose.rotation).normalized();
}

Eigen::Matrix4f DrawnTransform(const SmoothedPose& pose, const Eigen::Matrix4f& target) {
  Eigen::Matrix4f drawn = target;
  drawn.block<3, 3>(0, 0) = pose.rotation.toRotationMatrix();
  SetTranslation(pose.position, drawn);
  return drawn;
}

}  // namespace z13::math
