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

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include <Eigen/Dense>

#include <lib_core/math.h>
#include <lib_core/rollback.h>
#include <lib_core/simulation_clock.h>
#include <lib_core/world_snapshot_history.h>
#include <lib_core/world_state.h>

#include <z13/components/building.h>
#include <z13/components/gameplay.h>
#include <z13/components/player_action.h>

#include "../support/building_test_helpers.h"
#include "../support/z13_test_world.h"

// Two independent clients (worlds) act only in their own world; their actions are then
// "delivered" to the other world, both roll back and replay the combined, sorted action
// history, and must end up in the same state -- including when two players' actions
// land on the exact same tick.
namespace z13::state {
namespace {

namespace ft = z13::flecs_tools;
using z13::building::BasicBlock;
using z13::building::BuildingTool;
using z13::gameplay::PlayerActionLog;
using z13::gameplay::PlayerActionRecord;
using z13::testing::Click;
using z13::testing::kTestEpsilon;
using z13::testing::KeyDown;
using z13::testing::KeyUp;
using z13::testing::Z13TestWorld;
using Keycode = z13::fbs::input::Keycode;

constexpr uint32_t kPlayerAId = 0;
constexpr uint32_t kPlayerBId = 1;
constexpr std::string_view kRemotePlayerEntityName = "RemotePlayer";

float RealDeltaTime(Z13TestWorld& test_world) {
  return 1.f / test_world.Config().GetFPS();
}

// The catch-up runs on whatever delta its caller ticks with (see action_replay_test.cpp),
// so a live run must match it or replayed movement would diverge from the original.
void RealFrames(Z13TestWorld& test_world, uint64_t count) {
  const float delta_time = RealDeltaTime(test_world);
  for (uint64_t i = 0; i < count; ++i) {
    test_world.World().progress(delta_time);
  }
}

// Asks for the rollback and then ticks the world until it has caught up -- the same two
// steps Core::Update takes.
void RollBackTo(Z13TestWorld& test_world, uint64_t to_tick, uint64_t target_tick) {
  ft::RequestRollback(test_world.World(), to_tick, target_tick);
  ft::TickWorld(test_world.World(), RealDeltaTime(test_world));
}

uint64_t IntervalTicks(Z13TestWorld& test_world) {
  return static_cast<uint64_t>(
      std::llround(test_world.Config().GetSnapshotIntervalSeconds() * test_world.Config().GetFPS()));
}

void MoveForward(Z13TestWorld& test_world, uint64_t ticks) {
  test_world.EmitInput(KeyDown(Keycode::KEY_W));
  RealFrames(test_world, ticks);
  test_world.EmitInput(KeyUp(Keycode::KEY_W));
  test_world.World().progress(RealDeltaTime(test_world));
}

void MoveBackward(Z13TestWorld& test_world, uint64_t ticks) {
  test_world.EmitInput(KeyDown(Keycode::KEY_S));
  RealFrames(test_world, ticks);
  test_world.EmitInput(KeyUp(Keycode::KEY_S));
  test_world.World().progress(RealDeltaTime(test_world));
}

// A minimal player entity representing the other client's avatar. Never locally
// controlled -- no CurrentActionListenerTag, so live input and the recorder skip it;
// driven only by the replay. Starts at Identity like the real TestPlayer spawn does.
flecs::entity SpawnRemotePlayer(Z13TestWorld& test_world, uint32_t player_id) {
  flecs::world world = test_world.World();
  return world.entity(kRemotePlayerEntityName.data())
      .add<z13::flecs_tools::StateEntity>()
      .set(z13::gameplay::Player{.id = player_id})
      .set(Eigen::Matrix4f(Eigen::Matrix4f::Identity()))
      .set(z13::gameplay::PlayerCollider{.radius = z13::gameplay::kPlayerColliderRadius})
      .set(z13::input::ActionListener{});
}

flecs::entity FindPlayerById(flecs::world world, uint32_t player_id) {
  flecs::entity found;
  world.query_builder<const z13::gameplay::Player>().build().each(
      [&](flecs::entity e, const z13::gameplay::Player& player) {
        if (player.id == player_id) {
          found = e;
        }
      });
  return found;
}

// Every record this world logged for its own local player since `since_tick`.
std::vector<PlayerActionRecord> LocalActionsSince(Z13TestWorld& test_world, uint64_t since_tick) {
  std::vector<PlayerActionRecord> records;
  for (const auto& record : test_world.World().get<PlayerActionLog>().log.Entries()) {
    if (record.tick > since_tick) {
      records.push_back(record);
    }
  }
  return records;
}

bool RecordLess(const PlayerActionRecord& a, const PlayerActionRecord& b) {
  return std::tie(a.tick, a.player_id, a.action_id) < std::tie(b.tick, b.player_id, b.action_id);
}

// "Delivers" `incoming` as if received over the network: merges it into the log, sorted
// by (tick, player_id, action_id) -- a deterministic tie-break for same-tick actions.
void ReceiveForeignActions(Z13TestWorld& test_world, std::vector<PlayerActionRecord> incoming) {
  std::ranges::sort(incoming, RecordLess);
  test_world.World().get_mut<PlayerActionLog>().log.MergeSorted(std::move(incoming), RecordLess);
}

std::vector<Eigen::Vector3f> BlockPositions(flecs::world world) {
  std::vector<Eigen::Vector3f> positions;
  world.query_builder<const BasicBlock, const Eigen::Matrix4f>().build().each(
      [&](const BasicBlock&, const Eigen::Matrix4f& transform) {
        positions.push_back(z13::math::ExtractTranslation<float>(transform));
      });
  std::ranges::sort(positions, [](const Eigen::Vector3f& a, const Eigen::Vector3f& b) {
    return std::lexicographical_compare(a.data(), a.data() + 3, b.data(), b.data() + 3);
  });
  return positions;
}

std::set<std::string> BlockNames(flecs::world world) {
  std::set<std::string> names;
  world.query_builder<const BasicBlock>().build().each(
      [&](flecs::entity e, const BasicBlock&) { names.insert(e.name().c_str()); });
  return names;
}

TEST(TwoPlayerReplayTest, IndependentPlayersMergeAndReplayToIdenticalWorlds) {
  Z13TestWorld world_a;
  Z13TestWorld world_b;
  // Bootstrap assigns local player id 0 in both worlds -- give world_b's a distinct id.
  // LocalPlayer.id must follow it, or the next progress() strips world_b's own listener.
  world_b.Player().set(z13::gameplay::Player{.id = kPlayerBId});
  world_b.World().set<z13::gameplay::LocalPlayer>({.id = kPlayerBId});

  SpawnRemotePlayer(world_a, kPlayerBId);
  SpawnRemotePlayer(world_b, kPlayerAId);

  const uint64_t interval_ticks = IntervalTicks(world_a);
  ASSERT_EQ(interval_ticks, IntervalTicks(world_b));

  // A common baseline to roll back to later -- identical in both worlds so far.
  RealFrames(world_a, interval_ticks);
  RealFrames(world_b, interval_ticks);
  ASSERT_FALSE(world_a.World().get<ft::WorldSnapshotHistory>().history.Empty());
  ASSERT_FALSE(world_b.World().get<ft::WorldSnapshotHistory>().history.Empty());
  const ft::TimestampedSnapshot baseline_a = world_a.World().get<ft::WorldSnapshotHistory>().history.Entries().front();
  const ft::TimestampedSnapshot baseline_b = world_b.World().get<ft::WorldSnapshotHistory>().history.Entries().front();
  ASSERT_EQ(baseline_a.tick, baseline_b.tick);

  // Each player acts independently, only in their own world. Identical action shapes in
  // both worlds mean they end on the same tick -- and started on the same tick too, so
  // this exercises the same-tick-collision case below.
  z13::testing::EnterBuildMode(world_a);
  MoveForward(world_a, 10);
  Click(world_a, Keycode::MOUSE_BUTTON_LEFT);
  MoveForward(world_a, 5);
  Click(world_a, Keycode::MOUSE_BUTTON_LEFT);

  z13::testing::EnterBuildMode(world_b);
  MoveBackward(world_b, 10);
  Click(world_b, Keycode::MOUSE_BUTTON_LEFT);
  MoveBackward(world_b, 5);
  Click(world_b, Keycode::MOUSE_BUTTON_LEFT);

  const uint64_t target_tick = world_a.World().get<ft::SimulationClock>().tick;
  ASSERT_EQ(target_tick, world_b.World().get<ft::SimulationClock>().tick);

  const auto actions_from_a = LocalActionsSince(world_a, baseline_a.tick);
  const auto actions_from_b = LocalActionsSince(world_b, baseline_b.tick);
  ASSERT_FALSE(actions_from_a.empty());
  ASSERT_FALSE(actions_from_b.empty());

  // Confirm the scenario actually exercises the same-tick case.
  bool same_tick_from_both_players = false;
  for (const auto& a : actions_from_a) {
    for (const auto& b : actions_from_b) {
      same_tick_from_both_players |= (a.tick == b.tick);
    }
  }
  ASSERT_TRUE(same_tick_from_both_players)
      << "test scenario must exercise two different players acting on the same tick";

  // "Deliver" each player's actions to the other world, as network transport would.
  ReceiveForeignActions(world_a, actions_from_b);
  ReceiveForeignActions(world_b, actions_from_a);

  // Roll back to the shared baseline and replay the merged action history forward.
  RollBackTo(world_a, baseline_a.tick, target_tick);
  RollBackTo(world_b, baseline_b.tick, target_tick);
  ASSERT_FALSE(world_a.World().has<ft::RollbackFailed>());
  ASSERT_FALSE(world_b.World().has<ft::RollbackFailed>());

  // Each player's transform/build-mode agrees regardless of which world it's asked from.
  for (const uint32_t player_id : {kPlayerAId, kPlayerBId}) {
    const flecs::entity in_a = FindPlayerById(world_a.World(), player_id);
    const flecs::entity in_b = FindPlayerById(world_b.World(), player_id);
    ASSERT_TRUE(in_a.is_valid()) << "player " << player_id << " missing in world_a";
    ASSERT_TRUE(in_b.is_valid()) << "player " << player_id << " missing in world_b";
    EXPECT_TRUE(in_a.get<Eigen::Matrix4f>().isApprox(in_b.get<Eigen::Matrix4f>(), kTestEpsilon))
        << "player " << player_id;
    EXPECT_EQ(in_a.has<BuildingTool>(), in_b.has<BuildingTool>()) << "player " << player_id;
  }

  const auto positions_a = BlockPositions(world_a.World());
  const auto positions_b = BlockPositions(world_b.World());
  ASSERT_EQ(positions_a.size(), positions_b.size());
  for (size_t i = 0; i < positions_a.size(); ++i) {
    EXPECT_TRUE(positions_a[i].isApprox(positions_b[i], kTestEpsilon)) << "block " << i;
  }

  // Names can depend on ECS iteration order on a same-tick collision, unlike positions.
  EXPECT_EQ(BlockNames(world_a.World()), BlockNames(world_b.World()))
      << "block names may legitimately differ by ECS iteration order on a same-tick collision";
}

}  // namespace
}  // namespace z13::state
