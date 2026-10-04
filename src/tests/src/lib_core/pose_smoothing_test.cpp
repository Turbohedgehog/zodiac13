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

#include <cmath>

#include <Eigen/Dense>

#include <lib_core/utils/math.h>
#include <lib_core/utils/pose_smoothing.h>

namespace z13::math {
namespace {

constexpr float kFrameSeconds = 1.f / 60.f;
constexpr int kSettleFrames = 120;
constexpr float kEpsilon = 1e-4f;
constexpr PoseSmoothingParams kParams {.smooth_time_seconds = 0.1f, .snap_distance = 10.f, .snap_angle_rad = 2.f};

Eigen::Matrix4f At(const Eigen::Vector3f& position, float yaw_rad = 0.f) {
  Eigen::Matrix4f transform = Eigen::Matrix4f::Identity();
  transform.block<3, 3>(0, 0) = Eigen::AngleAxisf(yaw_rad, Eigen::Vector3f::UnitZ()).toRotationMatrix();
  SetTranslation(position, transform);
  return transform;
}

float Yaw(const SmoothedPose& pose) {
  const Eigen::Vector3f forward = pose.rotation * Eigen::Vector3f::UnitX();
  return std::atan2(forward.y(), forward.x());
}

TEST(PoseSmoothingTest, AStepGlidesInWithoutOvershoot) {
  SmoothedPose pose = PoseAt(At(Eigen::Vector3f::Zero()));
  const Eigen::Matrix4f target = At(Eigen::Vector3f(3.f, 0.f, 0.f));
  float previous = 0.f;
  for (int frame = 0; frame < kSettleFrames; ++frame) {
    ChasePose(pose, target, kFrameSeconds, kParams);
    EXPECT_GE(pose.position.x(), previous) << "moved back at frame " << frame;
    EXPECT_LE(pose.position.x(), 3.f) << "overshot at frame " << frame;
    previous = pose.position.x();
  }
  EXPECT_NEAR(pose.position.x(), 3.f, kEpsilon);
}

TEST(PoseSmoothingTest, AStepIsNotDrawnAtOnce) {
  SmoothedPose pose = PoseAt(At(Eigen::Vector3f::Zero()));
  ChasePose(pose, At(Eigen::Vector3f(3.f, 0.f, 0.f)), kFrameSeconds, kParams);

  EXPECT_GT(pose.position.x(), 0.f);
  EXPECT_LT(pose.position.x(), 1.f);
}

// The target stops while the pose is still moving towards it: it settles, never passes it.
TEST(PoseSmoothingTest, AStoppedTargetIsNotPassed) {
  constexpr float kSpeed = 0.5f;
  constexpr int kMoveFrames = 60;
  SmoothedPose pose = PoseAt(At(Eigen::Vector3f::Zero()));
  for (int frame = 1; frame <= kMoveFrames; ++frame) {
    ChasePose(pose, At(Eigen::Vector3f(kSpeed * static_cast<float>(frame), 0.f, 0.f)), kFrameSeconds, kParams);
  }
  const float stop = kSpeed * kMoveFrames;
  for (int frame = 0; frame < kSettleFrames; ++frame) {
    ChasePose(pose, At(Eigen::Vector3f(stop, 0.f, 0.f)), kFrameSeconds, kParams);
    EXPECT_LE(pose.position.x(), stop + kEpsilon) << "overshot at frame " << frame;
  }
  EXPECT_NEAR(pose.position.x(), stop, kEpsilon);
}

TEST(PoseSmoothingTest, ATurnGlidesInWithoutOvershoot) {
  constexpr float kTurnRad = 1.f;
  SmoothedPose pose = PoseAt(At(Eigen::Vector3f::Zero()));
  const Eigen::Matrix4f target = At(Eigen::Vector3f::Zero(), kTurnRad);
  float previous = 0.f;
  for (int frame = 0; frame < kSettleFrames; ++frame) {
    ChasePose(pose, target, kFrameSeconds, kParams);
    EXPECT_GE(Yaw(pose), previous - kEpsilon) << "turned back at frame " << frame;
    EXPECT_LE(Yaw(pose), kTurnRad + kEpsilon) << "overshot at frame " << frame;
    previous = Yaw(pose);
  }
  EXPECT_NEAR(Yaw(pose), kTurnRad, kEpsilon);
}

TEST(PoseSmoothingTest, AnErrorPastTheSnapLimitJumps) {
  SmoothedPose pose = PoseAt(At(Eigen::Vector3f::Zero()));
  const Eigen::Vector3f far(kParams.snap_distance * 2.f, 0.f, 0.f);
  ChasePose(pose, At(far), kFrameSeconds, kParams);

  EXPECT_TRUE(pose.position.isApprox(far));
  EXPECT_TRUE(pose.velocity.isZero());
}

TEST(PoseSmoothingTest, ZeroSmoothTimeDrawsTheTarget) {
  SmoothedPose pose = PoseAt(At(Eigen::Vector3f::Zero()));
  const Eigen::Matrix4f target = At(Eigen::Vector3f(1.f, 2.f, 3.f), 0.5f);
  ChasePose(pose, target, kFrameSeconds, {.snap_distance = 10.f, .snap_angle_rad = 2.f});

  EXPECT_TRUE(DrawnTransform(pose, target).isApprox(target, kEpsilon));
}

}  // namespace
}  // namespace z13::math
