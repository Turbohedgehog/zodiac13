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
#include <cstdint>
#include <memory>
#include <string>

#include <Eigen/Dense>

#include <lib_core/math.h>
#include <lib_core/simulation_clock.h>

#include <net_module/in_memory_transport.h>

#include <z13/components/building.h>
#include <z13/components/gameplay.h>
#include <z13/components/net.h>
#include <z13/components/player_action.h>
#include <z13_module/gameplay/camera_look.h>
#include <z13_module/gameplay/gameplay_entities.h>

#include "../support/building_test_helpers.h"
#include "../support/test_network.h"
#include "../support/z13_test_world.h"

// A player's input only reaches the others as a command scheduled for a tick everyone
// agrees on (docs/client-server-plan.md, "Модель синхронизации").
namespace z13::net {
namespace {

namespace ft = z13::flecs_tools;
using z13::testing::KeyDown;
using z13::testing::KeyUp;
using z13::testing::MouseDown;
using z13::testing::MouseUp;
using z13::testing::RunNetworkUntil;
using z13::testing::Z13TestWorld;

constexpr float kTestDeltaTime = 1.f / 60.f;
constexpr uint64_t kMaxTicks = 400;
constexpr std::string_view kServerEndpoint = "127.0.0.1:26213";
constexpr uint32_t kClientAId = 1;

Z13TestWorld MakeServer(const std::shared_ptr<InMemoryNetwork>& network) {
  return Z13TestWorld(/*skip_main_menu=*/false, {"--server"}, network);
}

Z13TestWorld MakeClient(const std::shared_ptr<InMemoryNetwork>& network) {
  return Z13TestWorld(/*skip_main_menu=*/false, {"--connect", std::string(kServerEndpoint)}, network);
}

bool IsConnected(Z13TestWorld& world) {
  return world.World().has<z13::gameplay::Gameplay>() &&
      world.World().get<ConnectionStatus>().state == ConnectionState::kConnected;
}

size_t QueuedFor(Z13TestWorld& world, uint32_t player_id) {
  const auto& records = world.World().get<z13::gameplay::ScheduledCommands>().records;
  return static_cast<size_t>(std::ranges::count_if(
      records, [player_id](const z13::gameplay::PlayerActionRecord& r) { return r.player_id == player_id; }));
}

bool IsSortedByApplyOrder(Z13TestWorld& world) {
  const auto& records = world.World().get<z13::gameplay::ScheduledCommands>().records;
  return std::ranges::is_sorted(records, [](const auto& a, const auto& b) {
    return std::tie(a.tick, a.player_id, a.action_id) < std::tie(b.tick, b.player_id, b.action_id);
  });
}

uint64_t SnapshotIntervalTicks(Z13TestWorld& world) {
  return static_cast<uint64_t>(std::llround(world.Config().GetSnapshotIntervalSeconds() * world.Config().GetFPS()));
}

size_t LogCountFor(Z13TestWorld& world, uint32_t player_id) {
  return static_cast<size_t>(std::ranges::count_if(
      world.World().get<z13::gameplay::PlayerActionLog>().log.Entries(),
      [player_id](const z13::gameplay::PlayerActionRecord& r) { return r.player_id == player_id; }));
}

size_t BlockCount(flecs::world world) {
  size_t count = 0;
  world.query_builder<const z13::building::BasicBlock>().build().each(
      [&](const z13::building::BasicBlock&) { ++count; });
  return count;
}

TEST(CommandStreamTest, AClientsCommandReachesEveryoneOnceWithNoServerEcho) {
  auto network = std::make_shared<InMemoryNetwork>();
  Z13TestWorld server = MakeServer(network);
  Z13TestWorld client_a = MakeClient(network);
  Z13TestWorld client_b = MakeClient(network);

  ASSERT_TRUE(RunNetworkUntil(*network, {server, client_a, client_b}, kTestDeltaTime, kMaxTicks, [&] {
    return IsConnected(client_a) && IsConnected(client_b);
  }));
  ASSERT_EQ(client_a.World().get<z13::gameplay::LocalPlayer>().id, kClientAId);

  client_a.EmitInput(KeyDown(z13::fbs::input::Keycode::KEY_W));
  ASSERT_TRUE(RunNetworkUntil(*network, {server, client_a, client_b}, kTestDeltaTime, kMaxTicks, [&] {
    return QueuedFor(server, kClientAId) > 0 && QueuedFor(client_b, kClientAId) > 0;
  })) << "the command never reached both the server and the other client";

  EXPECT_EQ(QueuedFor(client_a, kClientAId), 1u) << "the sender queues its own command locally, once, with no server echo on top";
  EXPECT_TRUE(IsSortedByApplyOrder(server));
  EXPECT_TRUE(IsSortedByApplyOrder(client_b));

  // Scheduling puts it ahead of the server's own clock, not on whatever tick it landed.
  const auto& queued = server.World().get<z13::gameplay::ScheduledCommands>().records;
  EXPECT_GT(queued.front().tick, server.World().get<ft::SimulationClock>().tick);
}

TEST(CommandStreamTest, AnIdleClientSendsNoCommands) {
  auto network = std::make_shared<InMemoryNetwork>();
  Z13TestWorld server = MakeServer(network);
  Z13TestWorld client = MakeClient(network);

  ASSERT_TRUE(RunNetworkUntil(
      *network, {server, client}, kTestDeltaTime, kMaxTicks, [&] { return IsConnected(client); }));
  RunNetworkUntil(*network, {server, client}, kTestDeltaTime, 120, [] { return false; });

  EXPECT_TRUE(server.World().get<z13::gameplay::ScheduledCommands>().records.empty());
}

Eigen::Vector3f Position(flecs::entity player) {
  return z13::math::ExtractTranslation<float>(player.get<Eigen::Matrix4f>());
}

// Welcome's held_values exists for exactly this: a hold with no record near the join
// tick (PlayerActionRecorder only reasserts once per snapshot interval) would otherwise
// leave the joiner's copy of that player frozen until the next reassert.
TEST(CommandStreamTest, LateJoinMidHoldSeesTheHeldMovementImmediately) {
  auto network = std::make_shared<InMemoryNetwork>();
  Z13TestWorld server = MakeServer(network);
  Z13TestWorld client_a = MakeClient(network);

  ASSERT_TRUE(RunNetworkUntil(
      *network, {server, client_a}, kTestDeltaTime, kMaxTicks, [&] { return IsConnected(client_a); }));

  const flecs::entity a_on_server = server.World().lookup(z13::gameplay::PlayerEntityName(kClientAId).c_str());
  ASSERT_TRUE(a_on_server);

  // Relative to the spawn position (id * kSpawnSpacing, already nonzero), not an absolute
  // threshold -- otherwise this would trip on the spawn offset itself, before the delayed
  // command ever applies.
  const float spawn_x = Position(a_on_server).x();
  client_a.EmitInput(KeyDown(z13::fbs::input::Keycode::KEY_W));  // held, never released
  ASSERT_TRUE(RunNetworkUntil(*network, {server, client_a}, kTestDeltaTime, kMaxTicks, [&] {
    return Position(a_on_server).x() - spawn_x > 0.01f;
  })) << "the held command never took effect on the server";

  // client_b joins mid-hold, well before the next periodic reassert.
  Z13TestWorld client_b = MakeClient(network);
  ASSERT_TRUE(RunNetworkUntil(*network, {server, client_a, client_b}, kTestDeltaTime, kMaxTicks, [&] {
    return IsConnected(client_b);
  }));

  const flecs::entity a_on_b = client_b.World().lookup(z13::gameplay::PlayerEntityName(kClientAId).c_str());
  ASSERT_TRUE(a_on_b);
  const float position_at_join = Position(a_on_b).x();

  // Still held afterwards: without held_values, nothing in the replayed log window would
  // tell the joiner the key is still down, so this player would freeze right where the
  // snapshot left it instead of continuing to move. A client's own view of a remote
  // player necessarily lags the server by a tick or two (no prediction), so this checks
  // the joiner's own movement is continuous, not that it matches the server's clock tick
  // for tick.
  constexpr int kExtraTicks = 20;
  RunNetworkUntil(*network, {server, client_a, client_b}, kTestDeltaTime, kExtraTicks, [] { return false; });

  const float moved = Position(a_on_b).x() - position_at_join;
  const float expected = static_cast<float>(kExtraTicks) * z13::gameplay::kCameraVelocity * kTestDeltaTime;
  EXPECT_NEAR(moved, expected, z13::testing::kTestEpsilon)
      << "held_values didn't seed the joiner's view of an already-held key";
}

// The whole point of scheduling by apply_tick instead of applying live input directly:
// a client's own command must not move it before that tick arrives, even on its own copy.
TEST(CommandStreamTest, LocalCommandDoesNotMoveTheClientBeforeItsApplyTick) {
  auto network = std::make_shared<InMemoryNetwork>();
  Z13TestWorld server = MakeServer(network);
  Z13TestWorld client_a = MakeClient(network);

  ASSERT_TRUE(RunNetworkUntil(
      *network, {server, client_a}, kTestDeltaTime, kMaxTicks, [&] { return IsConnected(client_a); }));

  const flecs::entity local_player = client_a.Player();
  const float before = Position(local_player).x();

  client_a.EmitInput(KeyDown(z13::fbs::input::Keycode::KEY_W));
  // Well under kInputDelayTicks (12) plus NetActionSender's own batching interval, so
  // this checks the tick right after the press, not just "eventually delayed enough".
  RunNetworkUntil(*network, {server, client_a}, kTestDeltaTime, 3, [] { return false; });
  EXPECT_FLOAT_EQ(Position(local_player).x(), before)
      << "the client moved on its own command before that command's apply_tick -- that's prediction";

  ASSERT_TRUE(RunNetworkUntil(*network, {server, client_a}, kTestDeltaTime, kMaxTicks, [&] {
    return Position(local_player).x() - before > 0.01f;
  })) << "the command never took effect at all";
}

// apply_tick is computed from the sender's own clock and travels with the command, so
// jittered/reordered delivery must not change how long a hold lasts on the receiving
// end -- only how close to the wire it cuts it.
TEST(CommandStreamTest, HeldDurationSurvivesJitteredDelivery) {
  auto network = std::make_shared<InMemoryNetwork>();
  network->SetFaultConfig({.drop_probability = 0., .min_delay_ticks = 0, .max_delay_ticks = 2});
  Z13TestWorld server = MakeServer(network);
  Z13TestWorld client_a = MakeClient(network);
  Z13TestWorld client_b = MakeClient(network);

  ASSERT_TRUE(RunNetworkUntil(*network, {server, client_a, client_b}, kTestDeltaTime, kMaxTicks, [&] {
    return IsConnected(client_a) && IsConnected(client_b);
  }));

  const flecs::entity a_on_b = client_b.World().lookup(z13::gameplay::PlayerEntityName(kClientAId).c_str());
  ASSERT_TRUE(a_on_b);
  const float before = Position(a_on_b).x();

  constexpr uint64_t kHoldTicks = 20;
  constexpr uint64_t kSettleTicks = 30;
  client_a.EmitInput(KeyDown(z13::fbs::input::Keycode::KEY_W));
  RunNetworkUntil(*network, {server, client_a, client_b}, kTestDeltaTime, kHoldTicks, [] { return false; });
  client_a.EmitInput(KeyUp(z13::fbs::input::Keycode::KEY_W));
  RunNetworkUntil(*network, {server, client_a, client_b}, kTestDeltaTime, kSettleTicks, [] { return false; });

  const float moved = Position(a_on_b).x() - before;
  const float expected = static_cast<float>(kHoldTicks) * z13::gameplay::kCameraVelocity * kTestDeltaTime;
  EXPECT_NEAR(moved, expected, z13::testing::kTestEpsilon)
      << "jittered delivery changed how long the hold registered as lasting";
}

// Two clients building on the same tick is exactly the case the plan calls out as not
// needing its own network code: each just schedules its own command, and the same
// (tick, player_id, action_id) ordering every participant derives independently is what
// keeps the result identical everywhere, whichever order the packets actually arrive in.
TEST(CommandStreamTest, SimultaneousBuildsFromTwoClientsLandOnBothWorlds) {
  auto network = std::make_shared<InMemoryNetwork>();
  network->SetFaultConfig({.drop_probability = 0., .min_delay_ticks = 0, .max_delay_ticks = 2});
  Z13TestWorld server = MakeServer(network);
  Z13TestWorld client_a = MakeClient(network);
  Z13TestWorld client_b = MakeClient(network);

  ASSERT_TRUE(RunNetworkUntil(*network, {server, client_a, client_b}, kTestDeltaTime, kMaxTicks, [&] {
    return IsConnected(client_a) && IsConnected(client_b);
  }));

  constexpr uint64_t kSettleTicks = 30;
  client_a.EmitInput(KeyDown(z13::fbs::input::Keycode::KEY_TAB));
  client_b.EmitInput(KeyDown(z13::fbs::input::Keycode::KEY_TAB));
  RunNetworkUntil(*network, {server, client_a, client_b}, kTestDeltaTime, 1, [] { return false; });
  client_a.EmitInput(KeyUp(z13::fbs::input::Keycode::KEY_TAB));
  client_b.EmitInput(KeyUp(z13::fbs::input::Keycode::KEY_TAB));
  RunNetworkUntil(*network, {server, client_a, client_b}, kTestDeltaTime, kSettleTicks, [] { return false; });

  // Both presses land on the same local tick, so their apply_ticks are at most a couple
  // of ticks apart -- close enough to exercise the same scheduling window in practice.
  client_a.EmitInput(MouseDown(z13::fbs::input::Keycode::MOUSE_BUTTON_LEFT));
  client_b.EmitInput(MouseDown(z13::fbs::input::Keycode::MOUSE_BUTTON_LEFT));
  RunNetworkUntil(*network, {server, client_a, client_b}, kTestDeltaTime, 1, [] { return false; });
  client_a.EmitInput(MouseUp(z13::fbs::input::Keycode::MOUSE_BUTTON_LEFT));
  client_b.EmitInput(MouseUp(z13::fbs::input::Keycode::MOUSE_BUTTON_LEFT));
  RunNetworkUntil(*network, {server, client_a, client_b}, kTestDeltaTime, kSettleTicks, [] { return false; });

  EXPECT_EQ(BlockCount(server.World()), 2u);
  EXPECT_EQ(BlockCount(client_a.World()), 2u);
  EXPECT_EQ(BlockCount(client_b.World()), 2u);
}

// PlayerActionRecorder only re-sends a held action once per snapshot interval (the same
// cadence WorldSnapshotHistory captures on), not every tick -- this is what keeps an idle
// or held-key client's traffic flat instead of growing with however long the key is down.
TEST(CommandStreamTest, HeldKeyReassertsPeriodicallyNotEveryTick) {
  auto network = std::make_shared<InMemoryNetwork>();
  Z13TestWorld server = MakeServer(network);
  Z13TestWorld client_a = MakeClient(network);

  ASSERT_TRUE(RunNetworkUntil(
      *network, {server, client_a}, kTestDeltaTime, kMaxTicks, [&] { return IsConnected(client_a); }));

  const uint64_t interval_ticks = SnapshotIntervalTicks(server);
  const uint64_t hold_ticks = interval_ticks * 2 + 10;  // spans two reassert boundaries
  client_a.EmitInput(KeyDown(z13::fbs::input::Keycode::KEY_W));
  RunNetworkUntil(*network, {server, client_a}, kTestDeltaTime, hold_ticks, [] { return false; });
  client_a.EmitInput(KeyUp(z13::fbs::input::Keycode::KEY_W));

  const size_t log_count = LogCountFor(server, kClientAId);
  EXPECT_GE(log_count, 1u);
  EXPECT_LE(log_count, 4u) << "expected roughly one record per press/reassert boundary, not one per tick";
}

}  // namespace
}  // namespace z13::net
