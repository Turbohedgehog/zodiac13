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

#include <cstdint>

#include <Eigen/Dense>

#include <z13/components/input_events.h>
#include <z13/components/input_state.h>
#include <z13/components/player.h>
#include <z13_module/gameplay/camera_look.h>
#include <z13_settings/settings.h>
#include <z13_touch/touch_mapping.h>

#include "../support/z13_test_world.h"

// Touches through the real input pipeline (docs/android-plan.md, f/touch-input): they give the
// same actions as the keyboard and mouse.
namespace z13::gameplay::input {
namespace {

using z13::fbs::input::Keycode;
using z13::testing::kTestEpsilon;

const Eigen::Vector2f kScreen(1000.f, 500.f);
// In the stick's zone (the left half) and the look zone, away from the buttons.
const Eigen::Vector2f kStickTouch(200.f, 250.f);
const Eigen::Vector2f kLookTouch(600.f, 250.f);
constexpr uint64_t kStickFinger = 1;
constexpr uint64_t kLookFinger = 2;
constexpr uint64_t kButtonFinger = 3;

template <typename EventT>
EventT Touch(uint64_t finger, const Eigen::Vector2f& position) {
  EventT event;
  event.finger = finger;
  event.position = position;
  event.screen_size = kScreen;
  return event;
}

Eigen::Vector2f Position(flecs::entity player) {
  return player.get<Eigen::Matrix4f>().col(3).head<2>();
}

float StickRadius(z13::testing::Z13TestWorld& test_world) {
  return z13::input::touch::StickRadius(test_world.World().get<z13::TouchLayout>(), kScreen);
}

const z13::input::InputState& State(z13::testing::Z13TestWorld& test_world) {
  return test_world.World().get<z13::input::InputState>();
}

TEST(TouchInputTest, StickPushedUpMovesForwardLikeW) {
  z13::testing::Z13TestWorld test_world;
  auto player = test_world.Player();

  test_world.EmitInput(Touch<z13::input::TouchDownEvent>(kStickFinger, kStickTouch));
  test_world.EmitInput(Touch<z13::input::TouchMoveEvent>(
      kStickFinger, kStickTouch - Eigen::Vector2f(0.f, StickRadius(test_world))));
  test_world.Tick(1.f);
  EXPECT_TRUE(Position(player).isApprox(Eigen::Vector2f(z13::gameplay::kCameraVelocity, 0.f), kTestEpsilon));

  test_world.EmitInput(Touch<z13::input::TouchUpEvent>(kStickFinger, kStickTouch));
  test_world.Tick(1.f);
  test_world.Tick(1.f);
  EXPECT_TRUE(Position(player).isApprox(Eigen::Vector2f(z13::gameplay::kCameraVelocity, 0.f), kTestEpsilon));
}

TEST(TouchInputTest, JitterInsideTheDeadZoneQueuesNothing) {
  z13::testing::Z13TestWorld test_world;
  const float jitter = test_world.World().get<z13::TouchLayout>().stick_dead_zone * StickRadius(test_world) / 2.f;

  test_world.EmitInput(Touch<z13::input::TouchDownEvent>(kStickFinger, kStickTouch));
  for (const Eigen::Vector2f& offset : {Eigen::Vector2f(jitter, 0.f), Eigen::Vector2f(0.f, -jitter),
                                        Eigen::Vector2f(-jitter, jitter)}) {
    test_world.EmitInput(Touch<z13::input::TouchMoveEvent>(kStickFinger, kStickTouch + offset));
  }
  EXPECT_TRUE(State(test_world).pending.empty());
}

TEST(TouchInputTest, FingersKeepTheirControls) {
  z13::testing::Z13TestWorld test_world;
  auto player = test_world.Player();
  const auto& layout = test_world.World().get<z13::TouchLayout>();
  const Eigen::Vector2f jump = z13::input::touch::ButtonCenter(layout, z13::input::TouchControl::kJump, kScreen);

  test_world.EmitInput(Touch<z13::input::TouchDownEvent>(kStickFinger, kStickTouch));
  test_world.EmitInput(Touch<z13::input::TouchDownEvent>(kLookFinger, kLookTouch));
  test_world.EmitInput(Touch<z13::input::TouchDownEvent>(kButtonFinger, jump));
  // The look finger crosses into the stick's zone and the jump finger slides off its button:
  // neither changes what it works.
  test_world.EmitInput(Touch<z13::input::TouchMoveEvent>(kLookFinger, kLookTouch - Eigen::Vector2f(kScreen.y() / 2.f, 0.f)));
  test_world.EmitInput(Touch<z13::input::TouchMoveEvent>(kButtonFinger, jump - Eigen::Vector2f(0.f, kScreen.y() / 4.f)));
  test_world.Tick(1.f);

  // Left by half the screen's height turns left by half look_degrees.
  EXPECT_NEAR(player.get<z13::gameplay::LookAngles>().yaw_deg, layout.look_degrees / 2.f, kTestEpsilon);
  const auto& state = State(test_world);
  EXPECT_EQ(state.input_state[z13::input::KeyIndex(Keycode::TOUCH_JUMP)], 1.f);
  EXPECT_EQ(state.input_state[z13::input::KeyIndex(Keycode::TOUCH_STICK_LEFT)], 0.f);
}

TEST(TouchInputTest, TapWithinOneFrameReachesTheSimulation) {
  z13::testing::Z13TestWorld test_world;
  const Eigen::Vector2f use = z13::input::touch::ButtonCenter(test_world.World().get<z13::TouchLayout>(),
                                                               z13::input::TouchControl::kUse, kScreen);
  test_world.EmitInput(Touch<z13::input::TouchDownEvent>(kButtonFinger, use));
  test_world.EmitInput(Touch<z13::input::TouchUpEvent>(kButtonFinger, use));

  test_world.Tick(1.f);
  EXPECT_EQ(State(test_world).input_state[z13::input::KeyIndex(Keycode::TOUCH_USE)], 1.f);
  test_world.Tick(1.f);
  EXPECT_EQ(State(test_world).input_state[z13::input::KeyIndex(Keycode::TOUCH_USE)], 0.f);
}

}  // namespace
}  // namespace z13::gameplay::input
