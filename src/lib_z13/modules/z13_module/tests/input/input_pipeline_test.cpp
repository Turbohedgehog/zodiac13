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

#include <z13/components/gameplay.h>
#include <z13/components/input.h>
#include <z13_module/gameplay/camera_look.h>

#include "../support/z13_test_world.h"

// Exercises the real input pipeline end to end: emitted flecs events ->
// InputState -> CalculateInputValues -> ApplyMoveActionListener ->
// ApplyCameraMove, through an actual flecs::world (no raylib/SDL). The pure
// ApplyCameraMove math itself is already covered by camera_look_test.cpp;
// these tests only check that events actually reach it correctly.
namespace z13::gameplay::input {
namespace {

Eigen::Vector3f Position(flecs::entity player) {
  return player.get<Eigen::Matrix4f>().col(3).head<3>();
}

z13::input::KeyboardDownEvent KeyDown(z13::fbs::input::Keycode code) {
  z13::input::KeyboardDownEvent event;
  event.keycode.code = code;
  return event;
}

z13::input::KeyboardUpEvent KeyUp(z13::fbs::input::Keycode code) {
  z13::input::KeyboardUpEvent event;
  event.keycode.code = code;
  return event;
}

// A mouse-move event lands in InputState only for the frame it was emitted
// in; dt=0.01 is chosen so -delta.x * dt * mouse_sensitivity comes out round
// (100 * 0.01 * 5 = 5).
TEST(InputPipelineTest, MouseLookRotatesCameraThroughPipeline) {
  z13::testing::Z13TestWorld test_world;
  auto player = test_world.Player();
  ASSERT_TRUE(player.is_alive());

  // OnMouseMove reads world.delta_time() synchronously at emit time (the
  // observer fires immediately, not deferred to the next progress()), and
  // delta_time() is only set by a progress() call -- so warm up with the
  // same dt first, or the mouse delta gets scaled by a stale 0.
  test_world.World().progress(0.01f);

  z13::input::MouseMoveEvent move_event;
  move_event.delta = {.x = 100, .y = 0};
  test_world.EmitInput(move_event);

  test_world.World().progress(0.01f);

  ASSERT_TRUE(player.has<z13::gameplay::LookAngles>());
  // invert_x=false, delta.x=100 > 0 (mouse moved right) -> yaw_deg negative.
  EXPECT_NEAR(player.get<z13::gameplay::LookAngles>().yaw_deg, -5.f, 1e-3f);
  EXPECT_TRUE(Position(player).isZero(1e-4f));  // a turn alone shouldn't move the camera
}

// Holding a key across several frames accumulates position linearly;
// KeyboardUpEvent stops the movement.
TEST(InputPipelineTest, HeldForwardKeyMovesPositionUntilKeyUp) {
  z13::testing::Z13TestWorld test_world;
  auto player = test_world.Player();

  test_world.EmitInput(KeyDown(z13::fbs::input::Keycode::KEY_W));

  test_world.World().progress(1.f);
  EXPECT_TRUE(Position(player).isApprox(
      Eigen::Vector3f(z13::gameplay::kCameraVelocity, 0.f, 0.f), 1e-3f));

  test_world.World().progress(1.f);  // no new event -- key is still held
  EXPECT_TRUE(Position(player).isApprox(
      Eigen::Vector3f(2.f * z13::gameplay::kCameraVelocity, 0.f, 0.f), 1e-3f));

  test_world.EmitInput(KeyUp(z13::fbs::input::Keycode::KEY_W));
  test_world.World().progress(1.f);
  EXPECT_TRUE(Position(player).isApprox(
      Eigen::Vector3f(2.f * z13::gameplay::kCameraVelocity, 0.f, 0.f), 1e-3f));
}

// Analogous to camera_look_test.cpp's ForwardMoveFollowsCameraAfterTurning,
// but driven through real events instead of calling ApplyCameraMove
// directly. delta.x=-18, dt=1 -> -(-18) * 1 * 5 = 90 -- an exact 90 degree
// turn in a single frame.
TEST(InputPipelineTest, ForwardMoveFollowsCameraAfterMouseTurnThroughPipeline) {
  z13::testing::Z13TestWorld test_world;
  auto player = test_world.Player();

  // Warm-up progress(): see MouseLookRotatesCameraThroughPipeline above.
  test_world.World().progress(1.f);

  z13::input::MouseMoveEvent turn_event;
  turn_event.delta = {.x = -18, .y = 0};
  test_world.EmitInput(turn_event);
  test_world.World().progress(1.f);
  ASSERT_NEAR(player.get<z13::gameplay::LookAngles>().yaw_deg, 90.f, 1e-3f);

  test_world.EmitInput(KeyDown(z13::fbs::input::Keycode::KEY_W));
  test_world.World().progress(1.f);

  // A +90 deg yaw turn rotates the camera's forward axis from +X to +Y.
  EXPECT_TRUE(Position(player).isApprox(
      Eigen::Vector3f(0.f, z13::gameplay::kCameraVelocity, 0.f), 1e-3f));
}

}  // namespace
}  // namespace z13::gameplay::input
