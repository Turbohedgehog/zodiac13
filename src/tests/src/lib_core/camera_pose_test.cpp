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

#include <gtest/gtest.h>

#include <Eigen/Dense>

#include <lib_core/utils/camera_pose.h>

namespace z13 {
namespace {

constexpr float kAngleTolerance = 1e-3f;

TEST(CameraPoseTest, YawTurnsFromXTowardsYAndPitchLooksUp) {
  EXPECT_TRUE(Forward({.yaw_deg = 90.f}).isApprox(Eigen::Vector3f::UnitY(), kAngleTolerance));
  EXPECT_TRUE(Forward({.pitch_deg = 90.f}).isApprox(Eigen::Vector3f::UnitZ(), kAngleTolerance));
}

// The overlay shows PoseLookingAt; --render-tour-view looks along Forward.
TEST(CameraPoseTest, APoseShownLooksWhereItWasTaken) {
  const Eigen::Vector3f eye(1.f, 2.f, 3.f);
  const Eigen::Vector3f target = eye + Eigen::Vector3f(-2.f, 1.f, 0.5f);

  const CameraPose pose = PoseLookingAt(eye, target);

  EXPECT_TRUE(pose.eye.isApprox(eye));
  EXPECT_TRUE(Forward(pose).isApprox((target - eye).normalized(), kAngleTolerance));
}

}  // namespace
}  // namespace z13
