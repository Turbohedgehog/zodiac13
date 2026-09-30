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

#include <lib_core/math.h>
#include <lib_core/rollback.h>
#include <lib_core/simulation_clock.h>

#include <z13/components/gameplay.h>

#include "../support/building_test_helpers.h"
#include "../support/z13_test_world.h"

// Single-player: Pause freezes the whole simulation, clock included.
namespace z13::gameplay {
namespace {

namespace ft = z13::flecs_tools;
using z13::testing::KeyDown;
using z13::testing::kTestDeltaTime;
using z13::testing::Z13TestWorld;

// The freeze takes effect one frame after Pause changes.
constexpr int kFreezeLatencyFrames = 1;
constexpr int kPausedFrames = 20;

void Tick(Z13TestWorld& test_world, int frames) {
  for (int frame = 0; frame < frames; ++frame) {
    ft::TickWorld(test_world.World(), kTestDeltaTime);
  }
}

uint64_t ClockTick(Z13TestWorld& test_world) {
  return test_world.World().get<ft::SimulationClock>().tick;
}

Eigen::Vector3f Position(Z13TestWorld& test_world) {
  return z13::math::ExtractTranslation<float>(test_world.Player().get<Eigen::Matrix4f>());
}

TEST(PauseTest, SinglePlayerPauseFreezesTheClockAndThePlayer) {
  Z13TestWorld test_world;
  test_world.EmitInput(KeyDown(z13::fbs::input::Keycode::KEY_W));
  Tick(test_world, 1);

  test_world.World().add<Pause>();
  Tick(test_world, kFreezeLatencyFrames);
  ASSERT_TRUE(test_world.World().has<ft::SimulationFrozen>());
  const uint64_t frozen_tick = ClockTick(test_world);
  const Eigen::Vector3f frozen_position = Position(test_world);

  Tick(test_world, kPausedFrames);

  EXPECT_EQ(ClockTick(test_world), frozen_tick);
  EXPECT_LT((Position(test_world) - frozen_position).norm(), z13::testing::kTestEpsilon);
}

TEST(PauseTest, SinglePlayerResumesAfterUnpause) {
  Z13TestWorld test_world;
  test_world.World().add<Pause>();
  Tick(test_world, kFreezeLatencyFrames + kPausedFrames);
  const uint64_t frozen_tick = ClockTick(test_world);

  test_world.World().remove<Pause>();
  Tick(test_world, kFreezeLatencyFrames + kPausedFrames);

  EXPECT_FALSE(test_world.World().has<ft::SimulationFrozen>());
  EXPECT_EQ(ClockTick(test_world), frozen_tick + kPausedFrames);
}

TEST(PauseTest, HeldKeyDoesNotMoveThePlayerOnceResumedIfReleasedWhilePaused) {
  Z13TestWorld test_world;
  test_world.EmitInput(KeyDown(z13::fbs::input::Keycode::KEY_W));
  Tick(test_world, 1);
  test_world.World().add<Pause>();
  Tick(test_world, kFreezeLatencyFrames);
  test_world.EmitInput(z13::testing::KeyUp(z13::fbs::input::Keycode::KEY_W));
  Tick(test_world, kPausedFrames);
  const Eigen::Vector3f paused_position = Position(test_world);

  test_world.World().remove<Pause>();
  Tick(test_world, kFreezeLatencyFrames + kPausedFrames);

  EXPECT_LT((Position(test_world) - paused_position).norm(), z13::testing::kTestEpsilon);
}

TEST(PauseTest, MouseMotionWhilePausedDoesNotTurnTheCameraOnResume) {
  Z13TestWorld test_world;
  Tick(test_world, 1);
  test_world.World().add<Pause>();
  Tick(test_world, kFreezeLatencyFrames);
  const Eigen::Matrix4f paused_transform = test_world.Player().get<Eigen::Matrix4f>();

  constexpr int kMenuMouseDelta = 200;
  z13::input::MouseMoveEvent over_menu;
  over_menu.delta = {.x = kMenuMouseDelta, .y = kMenuMouseDelta};
  test_world.EmitInput(over_menu);
  Tick(test_world, kPausedFrames);
  test_world.World().remove<Pause>();
  Tick(test_world, kFreezeLatencyFrames + kPausedFrames);

  EXPECT_TRUE(test_world.Player().get<Eigen::Matrix4f>().isApprox(paused_transform, z13::testing::kTestEpsilon));
}

}  // namespace
}  // namespace z13::gameplay
