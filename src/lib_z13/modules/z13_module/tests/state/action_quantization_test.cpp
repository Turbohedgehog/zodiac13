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
#include <cstdint>
#include <limits>
#include <string_view>

#include <z13/components/input.h>
#include <z13/components/player_action.h>
#include <z13_tests/test_time.h>

#include "../support/z13_test_world.h"

namespace z13::gameplay {
namespace {

using z13::testing::kTestDeltaTime;
using z13::testing::Z13TestWorld;

constexpr std::string_view kHorizontalLook = "HORIZONTAL_LOOK";

z13::input::ActionInfo::IdType HorizontalLookId(Z13TestWorld& test_world) {
  const auto& by_name = test_world.World().get<z13::input::ActionMap>().action_map.get<z13::input::ActionMap::ActionNameTag>();
  const auto found = by_name.find(kHorizontalLook);
  EXPECT_NE(found, by_name.end());
  return found->id;
}

float LiveValue(Z13TestWorld& test_world, z13::input::ActionInfo::IdType action_id) {
  const auto holder = test_world.Player().get<z13::input::ActionListener>().Value(action_id);
  return holder ? **holder : 0.f;
}

// Turns at `delta_px` per tick; the test ticks are 1s, so sensitivity is degrees per pixel.
float MoveMouse(Z13TestWorld& test_world, int delta_px) {
  z13::input::MouseMoveEvent move_event;
  move_event.delta = {.x = delta_px, .y = 0};
  test_world.EmitInput(move_event);
  test_world.Tick(kTestDeltaTime);
  return LiveValue(test_world, HorizontalLookId(test_world));
}

void SetSensitivity(Z13TestWorld& test_world, float degrees_per_pixel) {
  test_world.World().get_mut<z13::input::InputConfig>().mouse_sensitivity = degrees_per_pixel;
  test_world.Tick(kTestDeltaTime);  // so the next mouse event sees a 1s frame
}

TEST(QuantizeActionValueTest, RoundTripsWithinOneQuantizationStep) {
  for (const float value : {0.f, 1.f, -1.f, 0.5f, -0.33f, 3.14f}) {
    EXPECT_NEAR(CanonicalActionValue(value), value, 1.f / kActionValueScale);
  }
}

TEST(QuantizeActionValueTest, BooleanActionsRoundTripExactly) {
  EXPECT_EQ(QuantizeActionValue(0.f), 0);
  EXPECT_EQ(CanonicalActionValue(1.f), 1.f);
}

TEST(QuantizeActionValueTest, CanonicalValuesAreStable) {
  for (const float value : {0.123456f, -7.891f, 250.005f}) {
    const float canonical = CanonicalActionValue(value);
    EXPECT_EQ(CanonicalActionValue(canonical), canonical);
  }
}

TEST(QuantizeActionValueTest, OutOfRangeValuesClampInsteadOfOverflowing) {
  EXPECT_EQ(QuantizeActionValue(1e9f), std::numeric_limits<int16_t>::max());
  EXPECT_EQ(QuantizeActionValue(-1e9f), std::numeric_limits<int16_t>::min());
}

// Single-player is not log-driven, so the listener itself must hold what the log replays.
TEST(ActionQuantizationTest, LivePlayUsesTheLoggedValue) {
  Z13TestWorld test_world;
  SetSensitivity(test_world, 0.123456f);

  const float live = MoveMouse(test_world, 1);

  const auto& records = test_world.World().get<PlayerActionLog>().log.Entries();
  ASSERT_FALSE(records.empty());
  EXPECT_EQ(records.back().action_id, HorizontalLookId(test_world));
  EXPECT_EQ(records.back().value, live);
  EXPECT_EQ(CanonicalActionValue(live), live);
  EXPECT_NE(live, 0.f);
}

TEST(ActionQuantizationTest, SlowMouseMovementIsNotRoundedAway) {
  Z13TestWorld test_world;
  constexpr float kDegreesPerPixel = 0.003f;  // under half a quantum per tick
  constexpr int kTicks = 20;
  SetSensitivity(test_world, kDegreesPerPixel);
  const float start = LiveValue(test_world, HorizontalLookId(test_world));

  float looked = start;
  for (int i = 0; i < kTicks; ++i) {
    looked = MoveMouse(test_world, -1);
  }

  EXPECT_NEAR(looked - start, kDegreesPerPixel * kTicks, 1.f / kActionValueScale);
}

TEST(ActionQuantizationTest, AStillMouseLeavesNoRemainderBehind) {
  Z13TestWorld test_world;
  constexpr float kDegreesPerPixel = 0.0025f;
  constexpr int kStillTicks = 5;
  SetSensitivity(test_world, kDegreesPerPixel);
  const float looked = MoveMouse(test_world, 2);  // half a step

  for (int i = 0; i < kStillTicks; ++i) {
    test_world.Tick(kTestDeltaTime);
    EXPECT_EQ(LiveValue(test_world, HorizontalLookId(test_world)), looked) << "tick " << i;
  }
}

}  // namespace
}  // namespace z13::gameplay
