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

#include <z13_module/gameplay/camera_look.h>

// Exercises ApplyCameraMove() directly: it's pure math, with no flecs/ECS,
// InputConfig, SDL or raylib dependency.
namespace z13::gameplay {
namespace {

constexpr float kDeltaTime = 1.f;  // keeps expected displacement == axis * kCameraVelocity

Eigen::Vector3f Position(const Eigen::Matrix4f& transform) {
  return transform.col(3).head<3>();
}

TEST(CameraLook, ForwardMoveTranslatesAlongForwardAxis) {
  LookAngles look;
  Eigen::Matrix4f transform = Eigen::Matrix4f::Identity();
  CameraMoveAxes axes;
  axes.forward = 1.f;

  ApplyCameraMove(axes, kDeltaTime, look, transform);

  EXPECT_TRUE(Position(transform).isApprox(Eigen::Vector3f(kCameraVelocity, 0.f, 0.f), 1e-4f));
}

TEST(CameraLook, OpposingMoveAxesCancelOut) {
  LookAngles look;
  Eigen::Matrix4f transform = Eigen::Matrix4f::Identity();
  CameraMoveAxes axes;
  axes.forward = 1.f;
  axes.backward = 1.f;
  axes.left = 1.f;
  axes.right = 1.f;
  axes.up = 1.f;
  axes.down = 1.f;

  ApplyCameraMove(axes, kDeltaTime, look, transform);

  EXPECT_TRUE(Position(transform).isZero(1e-4f));
}

TEST(CameraLook, StrafeAndVerticalMoveAlongSideAndUpAxes) {
  LookAngles look;
  Eigen::Matrix4f transform = Eigen::Matrix4f::Identity();
  CameraMoveAxes axes;
  axes.left = 1.f;
  axes.up = 1.f;

  ApplyCameraMove(axes, kDeltaTime, look, transform);

  EXPECT_TRUE(Position(transform).isApprox(Eigen::Vector3f(0.f, kCameraVelocity, kCameraVelocity), 1e-4f));
}

TEST(CameraLook, BackwardRightDownMoveOppositeTheirPairedAxis) {
  LookAngles look;
  Eigen::Matrix4f transform = Eigen::Matrix4f::Identity();
  CameraMoveAxes axes;
  axes.backward = 1.f;
  axes.right = 1.f;
  axes.down = 1.f;

  ApplyCameraMove(axes, kDeltaTime, look, transform);

  EXPECT_TRUE(Position(transform).isApprox(
      Eigen::Vector3f(-kCameraVelocity, -kCameraVelocity, -kCameraVelocity), 1e-4f));
}

TEST(CameraLook, DiagonalMoveCombinesAxesAsVectorSum) {
  LookAngles look;
  Eigen::Matrix4f transform = Eigen::Matrix4f::Identity();
  CameraMoveAxes axes;
  axes.forward = 1.f;
  axes.right = 1.f;

  ApplyCameraMove(axes, kDeltaTime, look, transform);

  EXPECT_TRUE(Position(transform).isApprox(Eigen::Vector3f(kCameraVelocity, -kCameraVelocity, 0.f), 1e-4f));
}

TEST(CameraLook, MoveDeltaScalesWithDeltaTime) {
  LookAngles look;
  Eigen::Matrix4f transform = Eigen::Matrix4f::Identity();
  CameraMoveAxes axes;
  axes.forward = 1.f;
  constexpr float kHalfStep = 0.5f;

  ApplyCameraMove(axes, kHalfStep, look, transform);

  EXPECT_TRUE(Position(transform).isApprox(Eigen::Vector3f(kCameraVelocity * kHalfStep, 0.f, 0.f), 1e-4f));
}

TEST(CameraLook, MoveAccumulatesAcrossFramesFromExistingPosition) {
  LookAngles look;
  Eigen::Matrix4f transform = Eigen::Matrix4f::Identity();
  transform.block<3, 1>(0, 3) = Eigen::Vector3f(5.f, 0.f, 0.f);
  CameraMoveAxes axes;
  axes.forward = 1.f;

  ApplyCameraMove(axes, kDeltaTime, look, transform);
  ApplyCameraMove(axes, kDeltaTime, look, transform);

  EXPECT_TRUE(Position(transform).isApprox(Eigen::Vector3f(5.f + 2.f * kCameraVelocity, 0.f, 0.f), 1e-3f));
}

TEST(CameraLook, NoMoveInputLeavesPositionUnchanged) {
  LookAngles look;
  Eigen::Matrix4f transform = Eigen::Matrix4f::Identity();
  transform.block<3, 1>(0, 3) = Eigen::Vector3f(1.f, 2.f, 3.f);
  CameraMoveAxes axes;  // all zero

  ApplyCameraMove(axes, kDeltaTime, look, transform);

  EXPECT_TRUE(Position(transform).isApprox(Eigen::Vector3f(1.f, 2.f, 3.f), 1e-4f));
}

TEST(CameraLook, MouseLookAccumulatesIntoYawAndPitch) {
  LookAngles look;
  Eigen::Matrix4f transform = Eigen::Matrix4f::Identity();
  CameraMoveAxes axes;
  axes.yaw_delta_deg = 30.f;
  axes.pitch_delta_deg = 10.f;

  ApplyCameraMove(axes, kDeltaTime, look, transform);

  EXPECT_FLOAT_EQ(look.yaw_deg, 30.f);
  EXPECT_FLOAT_EQ(look.pitch_deg, -10.f);
}

TEST(CameraLook, YawWrapsIntoHalfTurnRangeInsteadOfClamping) {
  LookAngles look;
  look.yaw_deg = 170.f;
  Eigen::Matrix4f transform = Eigen::Matrix4f::Identity();
  CameraMoveAxes axes;
  axes.yaw_delta_deg = 20.f;  // 170 + 20 = 190, should wrap to -170, not clamp at 180

  ApplyCameraMove(axes, kDeltaTime, look, transform);

  EXPECT_NEAR(look.yaw_deg, -170.f, 1e-3f);
}

TEST(CameraLook, PitchClampsAtLookLimits) {
  LookAngles look;
  Eigen::Matrix4f transform = Eigen::Matrix4f::Identity();
  CameraMoveAxes axes;
  axes.pitch_delta_deg = 200.f;  // far beyond the limit in either direction

  ApplyCameraMove(axes, kDeltaTime, look, transform);
  EXPECT_FLOAT_EQ(look.pitch_deg, -kMaxPitchDeg);

  look = {};
  transform = Eigen::Matrix4f::Identity();
  axes.pitch_delta_deg = -200.f;

  ApplyCameraMove(axes, kDeltaTime, look, transform);
  EXPECT_FLOAT_EQ(look.pitch_deg, kMaxPitchDeg);
}

TEST(CameraLook, ForwardMoveFollowsCameraAfterTurning) {
  LookAngles look;
  Eigen::Matrix4f transform = Eigen::Matrix4f::Identity();

  CameraMoveAxes turn;
  turn.yaw_delta_deg = 90.f;
  ApplyCameraMove(turn, kDeltaTime, look, transform);

  CameraMoveAxes move;
  move.forward = 1.f;
  ApplyCameraMove(move, kDeltaTime, look, transform);

  // Facing +90 deg yaw turns the forward axis from +X to +Y.
  EXPECT_TRUE(Position(transform).isApprox(Eigen::Vector3f(0.f, kCameraVelocity, 0.f), 1e-3f));
}

}  // namespace
}  // namespace z13::gameplay
