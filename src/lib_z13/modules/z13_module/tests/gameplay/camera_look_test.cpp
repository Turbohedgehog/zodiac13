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
#include <z13_module/gameplay/camera_look.h>
#include <z13/components/player_parts.h>

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

TEST(CameraLook, TurnLookAddsYawAndSubtractsPitch) {
  const LookAngles look = TurnLook({}, 30.f, 10.f);

  EXPECT_FLOAT_EQ(look.yaw_deg, 30.f);
  EXPECT_FLOAT_EQ(look.pitch_deg, -10.f);
}

TEST(CameraLook, YawWrapsIntoHalfTurnRangeInsteadOfClamping) {
  // 170 + 20 = 190, should wrap to -170, not clamp at 180.
  EXPECT_NEAR(TurnLook({.yaw_deg = 170.f}, 20.f, 0.f).yaw_deg, -170.f, 1e-3f);
}

TEST(CameraLook, PitchClampsAtLookLimits) {
  EXPECT_FLOAT_EQ(TurnLook({}, 0.f, 200.f).pitch_deg, -kMaxPitchDeg);
  EXPECT_FLOAT_EQ(TurnLook({}, 0.f, -200.f).pitch_deg, kMaxPitchDeg);
}

TEST(CameraLook, CameraMoveSetsTheLookAnglesItIsGiven) {
  LookAngles look {.yaw_deg = 45.f, .pitch_deg = 20.f};
  Eigen::Matrix4f transform = Eigen::Matrix4f::Identity();
  CameraMoveAxes axes;
  axes.absolute_look = {.yaw_deg = 10.f, .pitch_deg = -5.f};

  ApplyCameraMove(axes, kDeltaTime, look, transform);

  EXPECT_FLOAT_EQ(look.yaw_deg, 10.f);
  EXPECT_FLOAT_EQ(look.pitch_deg, -5.f);
}

TEST(CameraLook, LookAnglesFromTransformRecoversTheAngles) {
  LookAngles look;
  Eigen::Matrix4f transform = Eigen::Matrix4f::Identity();
  CameraMoveAxes axes;
  axes.absolute_look = {.yaw_deg = 60.f, .pitch_deg = -30.f};
  ApplyCameraMove(axes, kDeltaTime, look, transform);

  const LookAngles recovered = LookAnglesFromTransform(transform);

  EXPECT_NEAR(recovered.yaw_deg, 60.f, 1e-3f);
  EXPECT_NEAR(recovered.pitch_deg, -30.f, 1e-3f);
}

TEST(CameraLook, ForwardMoveFollowsCameraAfterTurning) {
  LookAngles look;
  Eigen::Matrix4f transform = Eigen::Matrix4f::Identity();

  CameraMoveAxes turn;
  turn.absolute_look.yaw_deg = 90.f;
  ApplyCameraMove(turn, kDeltaTime, look, transform);

  CameraMoveAxes move;
  move.forward = 1.f;
  move.absolute_look = look;
  ApplyCameraMove(move, kDeltaTime, look, transform);

  // Facing +90 deg yaw turns the forward axis from +X to +Y.
  EXPECT_TRUE(Position(transform).isApprox(Eigen::Vector3f(0.f, kCameraVelocity, 0.f), 1e-3f));
}

constexpr float kLookDownDeg = 60.f;
const WalkStep kWalk {
    .delta_time = 0.1f, .gravity = Eigen::Vector3f(0.f, 0.f, -10.f), .walk_speed = 5.f, .jump_speed = 4.f};

TEST(WalkMove, LookingDownStillWalksAlongTheFloor) {
  LookAngles look;
  Eigen::Matrix4f transform = Eigen::Matrix4f::Identity();
  PlayerMotion motion {.grounded = true};
  CameraMoveAxes axes;
  axes.forward = 1.f;
  axes.absolute_look.pitch_deg = kLookDownDeg;

  ApplyWalkMove(axes, kWalk, look, motion, transform);

  EXPECT_NEAR(Position(transform).x(), kWalk.walk_speed * kWalk.delta_time, 1e-4f);
  EXPECT_FLOAT_EQ(look.pitch_deg, kLookDownDeg);
}

TEST(WalkMove, DiagonalKeysDontWalkFaster) {
  LookAngles look;
  Eigen::Matrix4f transform = Eigen::Matrix4f::Identity();
  PlayerMotion motion {.grounded = true};
  CameraMoveAxes axes;
  axes.forward = 1.f;
  axes.left = 1.f;

  ApplyWalkMove(axes, kWalk, look, motion, transform);

  EXPECT_NEAR(Position(transform).head<2>().norm(), kWalk.walk_speed * kWalk.delta_time, 1e-4f);
}

TEST(WalkMove, JumpsOnlyFromTheGround) {
  LookAngles look;
  Eigen::Matrix4f transform = Eigen::Matrix4f::Identity();
  CameraMoveAxes axes;
  axes.up = 1.f;
  PlayerMotion grounded {.grounded = true};
  PlayerMotion airborne;

  ApplyWalkMove(axes, kWalk, look, grounded, transform);
  ApplyWalkMove(axes, kWalk, look, airborne, transform);

  EXPECT_LT(grounded.fall_speed, 0.f);
  EXPECT_FALSE(grounded.grounded);
  EXPECT_GT(airborne.fall_speed, 0.f);
}

TEST(WalkMove, FallsAlongTheGravity) {
  LookAngles look;
  Eigen::Matrix4f transform = Eigen::Matrix4f::Identity();
  PlayerMotion motion;

  ApplyWalkMove({}, kWalk, look, motion, transform);

  const float speed = kWalk.gravity.norm() * kWalk.delta_time;
  EXPECT_FLOAT_EQ(motion.fall_speed, speed);
  EXPECT_TRUE(Position(transform).isApprox(Eigen::Vector3f(0.f, 0.f, -speed * kWalk.delta_time)));
}

// A room tilted by 4 degrees about X: its floor leans, and up with it.
constexpr float kTiltDeg = 4.f;

Eigen::Vector3f TiltedUp() {
  return Eigen::AngleAxisf(z13::math::ToRadians(kTiltDeg), Eigen::Vector3f::UnitX()) * Eigen::Vector3f::UnitZ();
}

TEST(UpFrame, TakesPlusZToTheUpAndIsARotation) {
  const Eigen::Vector3f up = TiltedUp();

  const Eigen::Matrix3f frame = UpFrame(up);

  EXPECT_TRUE((frame * Eigen::Vector3f::UnitZ()).isApprox(up, 1e-5f));
  EXPECT_TRUE((frame * frame.transpose()).isApprox(Eigen::Matrix3f::Identity(), 1e-5f));
  EXPECT_NEAR(frame.determinant(), 1.f, 1e-5f);
}

TEST(UpFrame, IsTheIdentityForPlusZAndAHalfTurnForMinusZ) {
  EXPECT_TRUE(UpFrame(Eigen::Vector3f::UnitZ()).isApprox(Eigen::Matrix3f::Identity()));

  const Eigen::Matrix3f upside_down = UpFrame(-Eigen::Vector3f::UnitZ());
  EXPECT_TRUE((upside_down * Eigen::Vector3f::UnitZ()).isApprox(-Eigen::Vector3f::UnitZ(), 1e-5f));
  EXPECT_NEAR(upside_down.determinant(), 1.f, 1e-5f);
}

TEST(FollowUp, MovesPartWayAndStaysAUnitVector) {
  const Eigen::Vector3f target = TiltedUp();

  const Eigen::Vector3f once = FollowUp(Eigen::Vector3f::UnitZ(), target, 0.5f);

  EXPECT_NEAR(once.norm(), 1.f, 1e-5f);
  EXPECT_GT(once.dot(target), Eigen::Vector3f::UnitZ().dot(target));
  EXPECT_LT(once.dot(target), 1.f);
}

TEST(FollowUp, ReachesTheTargetByRepeatedSteps) {
  const Eigen::Vector3f target = TiltedUp();
  Eigen::Vector3f up = Eigen::Vector3f::UnitZ();

  for (int step = 0; step < 200; ++step) {
    up = FollowUp(up, target, 0.1f);
  }

  EXPECT_TRUE(up.isApprox(target, 1e-4f));
  EXPECT_TRUE(FollowUp(up, target, 0.f).isApprox(up));
  EXPECT_TRUE(FollowUp(up, Eigen::Vector3f::UnitZ(), 1.f).isApprox(Eigen::Vector3f::UnitZ()));
}

TEST(FollowUp, GoesStraightToATargetOppositeTheUp) {
  const Eigen::Vector3f up = FollowUp(Eigen::Vector3f::UnitZ(), -Eigen::Vector3f::UnitZ(), 0.5f);

  EXPECT_NEAR(up.norm(), 1.f, 1e-5f);
  EXPECT_TRUE(up.isApprox(-Eigen::Vector3f::UnitZ()));
}

TEST(CameraLook, ATiltedUprightTurnsTheWholeCameraWithoutChangingWhatYawAndPitchMean) {
  LookAngles look;
  Eigen::Matrix4f straight = Eigen::Matrix4f::Identity();
  Eigen::Matrix4f tilted = Eigen::Matrix4f::Identity();
  CameraMoveAxes axes;
  axes.absolute_look = {.yaw_deg = 30.f, .pitch_deg = 10.f};
  CameraMoveAxes tilted_axes = axes;
  tilted_axes.upright = TiltedUp();

  ApplyCameraMove(axes, kDeltaTime, look, straight);
  ApplyCameraMove(tilted_axes, kDeltaTime, look, tilted);

  const Eigen::Matrix3f frame = UpFrame(TiltedUp());
  EXPECT_TRUE(tilted.block<3, 3>(0, 0).isApprox(frame * straight.block<3, 3>(0, 0), 1e-5f));
  EXPECT_NEAR(tilted.block<3, 3>(0, 0).col(2).dot(TiltedUp()), std::cos(z13::math::ToRadians(10.f)), 1e-4f);
}

TEST(CameraLook, ThePitchOfALevelLookIsZeroInTheFrameOfTheUpright) {
  LookAngles look;
  Eigen::Matrix4f transform = Eigen::Matrix4f::Identity();
  CameraMoveAxes axes;
  axes.upright = TiltedUp();
  axes.absolute_look = {.yaw_deg = 70.f, .pitch_deg = 0.f};

  ApplyCameraMove(axes, kDeltaTime, look, transform);

  const Eigen::Vector3f forward = transform.block<3, 3>(0, 0).col(0);
  EXPECT_NEAR(forward.dot(TiltedUp()), 0.f, 1e-5f) << "level looks stay across the up";
  const LookAngles again = LookAnglesFromTransform(transform, TiltedUp());
  EXPECT_NEAR(again.yaw_deg, 70.f, 1e-3f);
  EXPECT_NEAR(again.pitch_deg, 0.f, 1e-3f);
}

TEST(WalkMove, OnATiltedFloorTheStepStaysAcrossTheGravity) {
  LookAngles look;
  Eigen::Matrix4f transform = Eigen::Matrix4f::Identity();
  PlayerMotion motion {.grounded = true};
  CameraMoveAxes axes;
  axes.forward = 1.f;
  axes.upright = TiltedUp();
  WalkStep step = kWalk;
  step.gravity = -TiltedUp() * kWalk.gravity.norm();

  ApplyWalkMove(axes, step, look, motion, transform);

  const Eigen::Vector3f moved = Position(transform);
  const Eigen::Vector3f down = step.gravity.normalized();
  const Eigen::Vector3f along_floor = moved - (moved.dot(down) * down);
  EXPECT_NEAR(along_floor.norm(), step.walk_speed * step.delta_time, 1e-4f);
}

}  // namespace
}  // namespace z13::gameplay
