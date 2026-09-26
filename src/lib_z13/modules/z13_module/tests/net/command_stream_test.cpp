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

}  // namespace
}  // namespace z13::net
