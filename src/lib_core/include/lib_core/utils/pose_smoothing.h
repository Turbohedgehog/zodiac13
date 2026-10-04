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

namespace z13::math {

struct PoseSmoothingParams {
  // Roughly how long a correction takes to settle; 0 draws the target as is.
  float smooth_time_seconds {};
  // An error past either limit (a teleport, a resync) jumps instead of gliding.
  float snap_distance {};
  float snap_angle_rad {};
};

// A drawn pose chasing a simulated one: a critically damped spring per position and
// rotation, so corrections glide in without ringing.
struct SmoothedPose {
  Eigen::Vector3f position = Eigen::Vector3f::Zero();
  Eigen::Vector3f velocity = Eigen::Vector3f::Zero();
  Eigen::Quaternionf rotation = Eigen::Quaternionf::Identity();
  Eigen::Vector3f angular_velocity = Eigen::Vector3f::Zero();
};

SmoothedPose PoseAt(const Eigen::Matrix4f& transform);

// Moves `pose` one frame of `delta_seconds` towards `target`. Never carries it past the
// target: an overshoot would be the very bounce the smoothing is there to hide.
void ChasePose(SmoothedPose& pose, const Eigen::Matrix4f& target, float delta_seconds, const PoseSmoothingParams& params);

// `target` with its rotation and translation replaced by the pose's.
Eigen::Matrix4f DrawnTransform(const SmoothedPose& pose, const Eigen::Matrix4f& target);

}  // namespace z13::math
