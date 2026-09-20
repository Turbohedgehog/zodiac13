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
#include <string>
#include <vector>

#include <Eigen/Dense>

#include <lib_core/math.h>
#include <lib_core/simulation_clock.h>
#include <lib_core/world_json_store.h>
#include <lib_core/world_snapshot_history.h>

#include <z13/components/building.h>
#include <z13/components/player_action.h>
#include <z13_module/state/replay.h>

#include "../support/building_test_helpers.h"
#include "../support/world_json_test_helpers.h"
#include "../support/z13_test_world.h"

// Rollback/replay only makes sense if the engine itself is deterministic: this runs the
// same actions forward-only on two independent worlds and compares state at several
// checkpoints, including Bullet's destroy raycast.
namespace z13::state {
namespace {

namespace ft = z13::flecs_tools;
using z13::testing::Click;
using z13::testing::kTestDeltaTime;
using z13::testing::Z13TestWorld;
using Keycode = z13::fbs::input::Keycode;

constexpr float kBrushDistance = 5.f;
constexpr float kBlockColumnX = 20.f;
constexpr float kBlockSpacing = 6.f;
constexpr float kMouseTestDeltaTime = 0.01f;
constexpr int kMouseTestDelta = 100;

Eigen::Vector3f BlockAt(int index) {
  return {kBlockColumnX, kBlockSpacing * static_cast<float>(index), 0.f};
}

Eigen::Matrix4f AtPosition(const Eigen::Vector3f& position) {
  Eigen::Matrix4f transform = Eigen::Matrix4f::Identity();
  z13::math::SetTranslation(position, transform);
  return transform;
}

void MovePlayerTo(Z13TestWorld& test_world, const Eigen::Vector3f& position) {
  test_world.Player().set(AtPosition(position));
  test_world.World().progress(kTestDeltaTime);
}

// Player faces +X with no look angles applied, so block/brush geometry is simple.
void BuildBlockAt(Z13TestWorld& test_world, const Eigen::Vector3f& position) {
  MovePlayerTo(test_world, position - Eigen::Vector3f(kBrushDistance, 0.f, 0.f));
  Click(test_world, Keycode::MOUSE_BUTTON_LEFT);
}

void DestroyBlockAt(Z13TestWorld& test_world, const Eigen::Vector3f& position) {
  MovePlayerTo(test_world, position - Eigen::Vector3f(kBrushDistance, 0.f, 0.f));
  Click(test_world, Keycode::MOUSE_BUTTON_RIGHT);
}

// Turns the camera by yaw -5 degrees through the mouse-look pipeline.
void LookRight(Z13TestWorld& test_world) {
  test_world.World().progress(kMouseTestDeltaTime);
  z13::input::MouseMoveEvent move_event;
  move_event.delta = {.x = kMouseTestDelta, .y = 0};
  test_world.EmitInput(move_event);
  test_world.World().progress(kMouseTestDeltaTime);
}

std::string Checkpoint(Z13TestWorld& test_world) {
  const auto json = ft::WorldJsonStore::Save(test_world.World());
  EXPECT_TRUE(json.has_value()) << (json ? "" : json.error());
  return z13::testing::WithNormalizedSimulationTick(json.value_or(""));
}

uint64_t RoundTicks(double seconds, double ticks_per_second) {
  return static_cast<uint64_t>(std::llround(seconds * ticks_per_second));
}

uint64_t IntervalTicks(Z13TestWorld& test_world) {
  return RoundTicks(test_world.Config().GetSnapshotIntervalSeconds(), test_world.Config().GetFPS());
}

void Frames(Z13TestWorld& test_world, uint64_t count) {
  for (uint64_t i = 0; i < count; ++i) {
    test_world.World().progress(kTestDeltaTime);
  }
}

// Replay::Run derives its delta from Config::GetFPS(); kTestDeltaTime is a fixed 1s for
// other tests' readability, so post-rollback movement here must use this real delta
// instead or replayed positions would diverge from live ones.
float RealDeltaTime(Z13TestWorld& test_world) {
  return 1.f / test_world.Config().GetFPS();
}

void RealFrames(Z13TestWorld& test_world, uint64_t count) {
  const float delta_time = RealDeltaTime(test_world);
  for (uint64_t i = 0; i < count; ++i) {
    test_world.World().progress(delta_time);
  }
}

// Moves via real WASD input, not a .set() teleport like MovePlayerTo -- only input-driven
// changes reach PlayerActionLog.
void MoveForward(Z13TestWorld& test_world, uint64_t ticks) {
  test_world.EmitInput(z13::testing::KeyDown(Keycode::KEY_W));
  RealFrames(test_world, ticks);
  test_world.EmitInput(z13::testing::KeyUp(Keycode::KEY_W));
  test_world.World().progress(RealDeltaTime(test_world));
}

void LookRightReal(Z13TestWorld& test_world) {
  const float delta_time = RealDeltaTime(test_world);
  test_world.World().progress(delta_time);
  z13::input::MouseMoveEvent move_event;
  move_event.delta = {.x = kMouseTestDelta, .y = 0};
  test_world.EmitInput(move_event);
  test_world.World().progress(delta_time);
}

// Mixed camera + build/destroy sequence, checkpointed after every stage so a divergence
// is caught close to its cause rather than only in a final diff.
std::vector<std::string> RunMixedSequence(Z13TestWorld& test_world) {
  std::vector<std::string> checkpoints;

  z13::testing::EnterBuildMode(test_world);
  BuildBlockAt(test_world, BlockAt(0));
  BuildBlockAt(test_world, BlockAt(1));
  BuildBlockAt(test_world, BlockAt(2));
  checkpoints.push_back(Checkpoint(test_world));

  DestroyBlockAt(test_world, BlockAt(1));
  checkpoints.push_back(Checkpoint(test_world));

  LookRight(test_world);
  BuildBlockAt(test_world, BlockAt(3));
  checkpoints.push_back(Checkpoint(test_world));

  DestroyBlockAt(test_world, BlockAt(0));
  DestroyBlockAt(test_world, BlockAt(2));
  checkpoints.push_back(Checkpoint(test_world));

  return checkpoints;
}

TEST(ActionReplayTest, ForwardOnlyRunIsDeterministicAcrossIndependentWorlds) {
  Z13TestWorld world_a;
  Z13TestWorld world_b;

  const auto checkpoints_a = RunMixedSequence(world_a);
  const auto checkpoints_b = RunMixedSequence(world_b);

  ASSERT_EQ(checkpoints_a.size(), checkpoints_b.size());
  for (size_t i = 0; i < checkpoints_a.size(); ++i) {
    EXPECT_EQ(checkpoints_a[i], checkpoints_b[i]) << "checkpoint " << i;
  }
}

// Roll back to an earlier snapshot, replay buffered actions forward, and check the
// result matches what live play produced at that same tick.
TEST(ActionReplayTest, RollbackAndReplayReproducesLiveStateAtTheSameTick) {
  Z13TestWorld test_world;
  flecs::world& world = test_world.World();
  const uint64_t interval_ticks = IntervalTicks(test_world);

  z13::testing::EnterBuildMode(test_world);
  BuildBlockAt(test_world, BlockAt(0));
  BuildBlockAt(test_world, BlockAt(1));

  // An earlier point to roll back to -- the start would only exercise RestoreWorld, not
  // the action replay this test is about.
  Frames(test_world, interval_ticks);
  ASSERT_FALSE(world.get<ft::WorldSnapshotHistory>().history.Empty());
  const ft::TimestampedSnapshot rollback_point =
      world.get<ft::WorldSnapshotHistory>().history.Entries().front();

  // Everything from here on is real input (movement, look, clicks) -- not a teleport --
  // so it's exactly what PlayerActionLog captures and Replay::Run has to reconstruct.
  MoveForward(test_world, 10);
  Click(test_world, Keycode::MOUSE_BUTTON_LEFT);
  LookRightReal(test_world);
  MoveForward(test_world, 5);
  Click(test_world, Keycode::MOUSE_BUTTON_LEFT);
  Click(test_world, Keycode::MOUSE_BUTTON_RIGHT);

  const uint64_t target_tick = world.get<z13::flecs_tools::SimulationClock>().tick;
  const std::string ground_truth = Checkpoint(test_world);

  const auto replayed = Replay::Run(world, rollback_point, target_tick);
  ASSERT_TRUE(replayed.has_value()) << replayed.error();

  EXPECT_EQ(world.get<z13::flecs_tools::SimulationClock>().tick, target_tick);
  EXPECT_EQ(Checkpoint(test_world), ground_truth);
}

// Targets a tick older than the log's retained_since_tick directly, to check the guard
// actually fires instead of silently reconstructing incomplete state.
TEST(ActionReplayTest, RunRejectsASnapshotOlderThanTheActionLogsRetentionWindow) {
  Z13TestWorld test_world;
  flecs::world& world = test_world.World();
  const uint64_t interval_ticks = IntervalTicks(test_world);
  const uint64_t retention_ticks = RoundTicks(test_world.Config().GetSnapshotRetentionSeconds(), test_world.Config().GetFPS());

  Frames(test_world, retention_ticks + interval_ticks);
  ASSERT_GT(world.get<z13::gameplay::PlayerActionLog>().retained_since_tick, 0u);

  const ft::TimestampedSnapshot stale_snapshot {.tick = 0, .snapshot = {}};
  const auto replayed = Replay::Run(world, stale_snapshot, world.get<z13::flecs_tools::SimulationClock>().tick);

  ASSERT_FALSE(replayed.has_value());
}

}  // namespace
}  // namespace z13::state
