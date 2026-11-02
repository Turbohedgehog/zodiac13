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

#include <array>
#include <cmath>

#include <Eigen/Dense>

#include <lib_core/utils/math.h>
#include <player/camera_look.h>
#include <z13/components/gameplay/player_parts.h>

// Exercises ApplyCameraMove() directly: it's pure math, with no flecs/ECS,
// InputConfig, SDL or raylib dependency.
namespace z13::gameplay {
namespace {

// z13::math::kEpsilon: what float arithmetic leaves of a position or a length of a few meters.
constexpr float kTolerance = static_cast<float>(z13::math::kEpsilon);
// Float rounding in a few products of rotation matrices.
constexpr float kRounding = 1e-5f;
// Positions of tens of meters summed over steps.
constexpr float kSumTolerance = 1e-3f;
// Degrees through an atan2 or an asin.
constexpr float kAngleToleranceDeg = 1e-3f;

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

  EXPECT_TRUE(Position(transform).isApprox(Eigen::Vector3f(kCameraVelocity, 0.f, 0.f), kTolerance));
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

  EXPECT_TRUE(Position(transform).isZero(kTolerance));
}

TEST(CameraLook, StrafeAndVerticalMoveAlongSideAndUpAxes) {
  LookAngles look;
  Eigen::Matrix4f transform = Eigen::Matrix4f::Identity();
  CameraMoveAxes axes;
  axes.left = 1.f;
  axes.up = 1.f;

  ApplyCameraMove(axes, kDeltaTime, look, transform);

  EXPECT_TRUE(Position(transform).isApprox(Eigen::Vector3f(0.f, kCameraVelocity, kCameraVelocity), kTolerance));
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
      Eigen::Vector3f(-kCameraVelocity, -kCameraVelocity, -kCameraVelocity), kTolerance));
}

TEST(CameraLook, DiagonalMoveCombinesAxesAsVectorSum) {
  LookAngles look;
  Eigen::Matrix4f transform = Eigen::Matrix4f::Identity();
  CameraMoveAxes axes;
  axes.forward = 1.f;
  axes.right = 1.f;

  ApplyCameraMove(axes, kDeltaTime, look, transform);

  EXPECT_TRUE(Position(transform).isApprox(Eigen::Vector3f(kCameraVelocity, -kCameraVelocity, 0.f), kTolerance));
}

TEST(CameraLook, MoveDeltaScalesWithDeltaTime) {
  LookAngles look;
  Eigen::Matrix4f transform = Eigen::Matrix4f::Identity();
  CameraMoveAxes axes;
  axes.forward = 1.f;
  constexpr float kHalfStep = 0.5f;

  ApplyCameraMove(axes, kHalfStep, look, transform);

  EXPECT_TRUE(Position(transform).isApprox(Eigen::Vector3f(kCameraVelocity * kHalfStep, 0.f, 0.f), kTolerance));
}

TEST(CameraLook, MoveAccumulatesAcrossFramesFromExistingPosition) {
  LookAngles look;
  Eigen::Matrix4f transform = Eigen::Matrix4f::Identity();
  transform.block<3, 1>(0, 3) = Eigen::Vector3f(5.f, 0.f, 0.f);
  CameraMoveAxes axes;
  axes.forward = 1.f;

  ApplyCameraMove(axes, kDeltaTime, look, transform);
  ApplyCameraMove(axes, kDeltaTime, look, transform);

  EXPECT_TRUE(Position(transform).isApprox(Eigen::Vector3f(5.f + 2.f * kCameraVelocity, 0.f, 0.f), kSumTolerance));
}

TEST(CameraLook, NoMoveInputLeavesPositionUnchanged) {
  LookAngles look;
  Eigen::Matrix4f transform = Eigen::Matrix4f::Identity();
  transform.block<3, 1>(0, 3) = Eigen::Vector3f(1.f, 2.f, 3.f);
  CameraMoveAxes axes;  // all zero

  ApplyCameraMove(axes, kDeltaTime, look, transform);

  EXPECT_TRUE(Position(transform).isApprox(Eigen::Vector3f(1.f, 2.f, 3.f), kTolerance));
}

TEST(CameraLook, TurnLookAddsYawAndSubtractsPitch) {
  const LookAngles look = TurnLook({}, 30.f, 10.f);

  EXPECT_FLOAT_EQ(look.yaw_deg, 30.f);
  EXPECT_FLOAT_EQ(look.pitch_deg, -10.f);
}

TEST(CameraLook, YawWrapsIntoHalfTurnRangeInsteadOfClamping) {
  // 170 + 20 = 190, should wrap to -170, not clamp at 180.
  EXPECT_NEAR(TurnLook({.yaw_deg = 170.f}, 20.f, 0.f).yaw_deg, -170.f, kAngleToleranceDeg);
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

  EXPECT_NEAR(recovered.yaw_deg, 60.f, kAngleToleranceDeg);
  EXPECT_NEAR(recovered.pitch_deg, -30.f, kAngleToleranceDeg);
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
  EXPECT_TRUE(Position(transform).isApprox(Eigen::Vector3f(0.f, kCameraVelocity, 0.f), kSumTolerance));
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

  EXPECT_NEAR(Position(transform).x(), kWalk.walk_speed * kWalk.delta_time, kTolerance);
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

  EXPECT_NEAR(Position(transform).head<2>().norm(), kWalk.walk_speed * kWalk.delta_time, kTolerance);
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

  EXPECT_TRUE((frame * Eigen::Vector3f::UnitZ()).isApprox(up, kRounding));
  EXPECT_TRUE((frame * frame.transpose()).isApprox(Eigen::Matrix3f::Identity(), kRounding));
  EXPECT_NEAR(frame.determinant(), 1.f, kRounding);
}

TEST(UpFrame, IsTheIdentityForPlusZAndAHalfTurnForMinusZ) {
  EXPECT_TRUE(UpFrame(Eigen::Vector3f::UnitZ()).isApprox(Eigen::Matrix3f::Identity()));

  const Eigen::Matrix3f upside_down = UpFrame(-Eigen::Vector3f::UnitZ());
  EXPECT_TRUE((upside_down * Eigen::Vector3f::UnitZ()).isApprox(-Eigen::Vector3f::UnitZ(), kRounding));
  EXPECT_NEAR(upside_down.determinant(), 1.f, kRounding);
}

TEST(UpFrame, StaysARotationWhereTheUpIsAlmostOppositeAndWhereItHasNoAxis) {
  // How far off the -Z axis the ups are: the cosine is within a few parts in a thousand, then a million, of -1.
  constexpr std::array kOffAxis = {0.03f, 1e-3f};
  // So near the axis that the sine is below the 1e-6 under which UpFrame finds no axis.
  constexpr float kNoAxis = 1e-7f;
  for (const float off_axis : kOffAxis) {
    const Eigen::Vector3f nearly_opposite = Eigen::Vector3f(0.f, off_axis, -1.f).normalized();

    const Eigen::Matrix3f frame = UpFrame(nearly_opposite);

    EXPECT_TRUE(frame.allFinite()) << off_axis;
    EXPECT_TRUE((frame * Eigen::Vector3f::UnitZ()).isApprox(nearly_opposite, kRounding)) << off_axis;
    EXPECT_TRUE((frame * frame.transpose()).isApprox(Eigen::Matrix3f::Identity(), kRounding)) << off_axis;
  }
  const Eigen::Matrix3f without_axis = UpFrame(Eigen::Vector3f(0.f, kNoAxis, -1.f).normalized());
  EXPECT_TRUE(without_axis.allFinite());
  EXPECT_NEAR(without_axis.determinant(), 1.f, kRounding);
}

TEST(FollowUp, MovesPartWayAndStaysAUnitVector) {
  const Eigen::Vector3f target = TiltedUp();

  const Eigen::Vector3f once = FollowUp(Eigen::Vector3f::UnitZ(), target, 0.5f);

  EXPECT_NEAR(once.norm(), 1.f, kRounding);
  EXPECT_GT(once.dot(target), Eigen::Vector3f::UnitZ().dot(target));
  EXPECT_LT(once.dot(target), 1.f);
}

TEST(FollowUp, ReachesTheTargetByRepeatedSteps) {
  const Eigen::Vector3f target = TiltedUp();
  Eigen::Vector3f up = Eigen::Vector3f::UnitZ();

  for (int step = 0; step < 200; ++step) {
    up = FollowUp(up, target, 0.1f);
  }

  EXPECT_TRUE(up.isApprox(target, kTolerance));
  EXPECT_TRUE(FollowUp(up, target, 0.f).isApprox(up));
  EXPECT_TRUE(FollowUp(up, Eigen::Vector3f::UnitZ(), 1.f).isApprox(Eigen::Vector3f::UnitZ()));
}

TEST(FollowUp, GoesStraightToATargetOppositeTheUp) {
  const Eigen::Vector3f up = FollowUp(Eigen::Vector3f::UnitZ(), -Eigen::Vector3f::UnitZ(), 0.5f);

  EXPECT_NEAR(up.norm(), 1.f, kRounding);
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
  const Eigen::Matrix3f tilted_rotation = tilted.block<3, 3>(0, 0);
  const Eigen::Matrix3f straight_rotation = straight.block<3, 3>(0, 0);
  EXPECT_TRUE(tilted_rotation.isApprox(frame * straight_rotation, kRounding));
  EXPECT_NEAR(tilted_rotation.col(2).dot(TiltedUp()), std::cos(z13::math::ToRadians(10.f)), kTolerance);
}

TEST(CameraLook, ThePitchOfALevelLookIsZeroInTheFrameOfTheUpright) {
  LookAngles look;
  Eigen::Matrix4f transform = Eigen::Matrix4f::Identity();
  CameraMoveAxes axes;
  axes.upright = TiltedUp();
  axes.absolute_look = {.yaw_deg = 70.f, .pitch_deg = 0.f};

  ApplyCameraMove(axes, kDeltaTime, look, transform);

  const Eigen::Vector3f forward = transform.block<3, 3>(0, 0).col(0);
  EXPECT_NEAR(forward.dot(TiltedUp()), 0.f, kRounding) << "level looks stay across the up";
  const LookAngles again = LookAnglesFromTransform(transform, TiltedUp());
  EXPECT_NEAR(again.yaw_deg, 70.f, kAngleToleranceDeg);
  EXPECT_NEAR(again.pitch_deg, 0.f, kAngleToleranceDeg);
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
  EXPECT_NEAR(along_floor.norm(), step.walk_speed * step.delta_time, kTolerance);
}

const DriftStep kDrift {.delta_time = 0.1f, .thrust = 4.f, .max_speed = 1.f};
constexpr int kManySteps = 50;

TEST(DriftMove, AKeyPushesAndTheSpeedStaysAfterItIsReleased) {
  LookAngles look;
  Eigen::Matrix4f transform = Eigen::Matrix4f::Identity();
  PlayerMotion motion;
  CameraMoveAxes pushing;
  pushing.forward = 1.f;

  ApplyDriftMove(pushing, kDrift, look, motion, transform);
  const Eigen::Vector3f speed = motion.drift;
  ApplyDriftMove({}, kDrift, look, motion, transform);

  EXPECT_NEAR(speed.x(), kDrift.thrust * kDrift.delta_time, kTolerance);
  EXPECT_TRUE(motion.drift.isApprox(speed)) << "nothing slows the drift";
  EXPECT_GT(Position(transform).x(), speed.x() * kDrift.delta_time);
}

TEST(DriftMove, TheSpeedStopsGrowingAtTheLimit) {
  LookAngles look;
  Eigen::Matrix4f transform = Eigen::Matrix4f::Identity();
  PlayerMotion motion;
  CameraMoveAxes pushing;
  pushing.forward = 1.f;

  for (int i = 0; i < kManySteps; ++i) {
    ApplyDriftMove(pushing, kDrift, look, motion, transform);
  }

  EXPECT_NEAR(motion.drift.norm(), kDrift.max_speed, kTolerance);
}

TEST(DriftMove, AnOppositePushBrakes) {
  LookAngles look;
  Eigen::Matrix4f transform = Eigen::Matrix4f::Identity();
  PlayerMotion motion {.drift = Eigen::Vector3f(kDrift.max_speed, 0.f, 0.f)};
  CameraMoveAxes braking;
  braking.backward = 1.f;

  ApplyDriftMove(braking, kDrift, look, motion, transform);

  EXPECT_LT(motion.drift.x(), kDrift.max_speed);
}

TEST(DriftMove, DiagonalKeysDontPushHarder) {
  LookAngles look;
  Eigen::Matrix4f transform = Eigen::Matrix4f::Identity();
  PlayerMotion motion;
  CameraMoveAxes diagonal;
  diagonal.forward = 1.f;
  diagonal.left = 1.f;

  ApplyDriftMove(diagonal, kDrift, look, motion, transform);

  EXPECT_NEAR(motion.drift.norm(), kDrift.thrust * kDrift.delta_time, kTolerance);
}

}  // namespace
}  // namespace z13::gameplay
