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

namespace z13::net {
namespace {

namespace ft = z13::flecs_tools;
using z13::testing::kConnectArg;
using z13::testing::kMaxNetTestTicks;
using z13::testing::kNetTestDeltaTime;
using z13::testing::kServerArg;
using z13::testing::kTestServerEndpoint;
using z13::testing::KeyDown;
using z13::testing::KeyUp;
using z13::testing::MouseDown;
using z13::testing::MouseUp;
using z13::testing::RunNetworkUntil;
using z13::testing::Z13TestWorld;

constexpr uint32_t kClientAId = 1;

Z13TestWorld MakeServer(const std::shared_ptr<InMemoryNetwork>& network) {
  return Z13TestWorld(/*skip_main_menu=*/false, {std::string(kServerArg)}, network);
}

Z13TestWorld MakeClient(const std::shared_ptr<InMemoryNetwork>& network) {
  return Z13TestWorld(/*skip_main_menu=*/false, {std::string(kConnectArg), std::string(kTestServerEndpoint)}, network);
}

bool IsConnected(Z13TestWorld& world) {
  return world.World().has<z13::gameplay::Gameplay>() &&
      world.World().get<ConnectionStatus>().state == ConnectionState::kConnected;
}

bool IsLogSorted(Z13TestWorld& world) {
  const auto& records = world.World().get<z13::gameplay::PlayerActionLog>().log.Entries();
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

  ASSERT_TRUE(RunNetworkUntil(*network, {server, client_a, client_b}, kNetTestDeltaTime, kMaxNetTestTicks, [&] {
    return IsConnected(client_a) && IsConnected(client_b);
  }));
  ASSERT_EQ(client_a.World().get<z13::gameplay::LocalPlayer>().id, kClientAId);

  client_a.EmitInput(KeyDown(z13::fbs::input::Keycode::KEY_W));
  ASSERT_TRUE(RunNetworkUntil(*network, {server, client_a, client_b}, kNetTestDeltaTime, kMaxNetTestTicks, [&] {
    return LogCountFor(server, kClientAId) > 0 && LogCountFor(client_b, kClientAId) > 0;
  })) << "the command never reached both the server and the other client";
  RunNetworkUntil(*network, {server, client_a, client_b}, kNetTestDeltaTime, 10, [] { return false; });

  EXPECT_EQ(LogCountFor(client_a, kClientAId), 1u) << "the sender logs its own command once, with no server echo on top";
  EXPECT_EQ(LogCountFor(server, kClientAId), 1u);
  EXPECT_EQ(LogCountFor(client_b, kClientAId), 1u);
  EXPECT_TRUE(IsLogSorted(server));
  EXPECT_TRUE(IsLogSorted(client_b));
}

TEST(CommandStreamTest, AnIdleClientSendsNoCommands) {
  auto network = std::make_shared<InMemoryNetwork>();
  Z13TestWorld server = MakeServer(network);
  Z13TestWorld client = MakeClient(network);

  ASSERT_TRUE(RunNetworkUntil(
      *network, {server, client}, kNetTestDeltaTime, kMaxNetTestTicks, [&] { return IsConnected(client); }));
  RunNetworkUntil(*network, {server, client}, kNetTestDeltaTime, 120, [] { return false; });

  EXPECT_TRUE(server.World().get<z13::gameplay::PlayerActionLog>().log.Entries().empty());
}

// Looked up every time: a rollback past a join recreates the player entity.
Eigen::Vector3f Position(Z13TestWorld& world, uint32_t player_id) {
  const flecs::entity player = world.World().lookup(z13::gameplay::PlayerEntityName(player_id).c_str());
  EXPECT_TRUE(player) << "no player " << player_id;
  return player ? z13::math::ExtractTranslation<float>(player.get<Eigen::Matrix4f>()) : Eigen::Vector3f::Zero();
}

TEST(CommandStreamTest, LateJoinMidHoldSeesTheHeldMovementImmediately) {
  auto network = std::make_shared<InMemoryNetwork>();
  Z13TestWorld server = MakeServer(network);
  Z13TestWorld client_a = MakeClient(network);

  ASSERT_TRUE(RunNetworkUntil(
      *network, {server, client_a}, kNetTestDeltaTime, kMaxNetTestTicks, [&] { return IsConnected(client_a); }));

  const float spawn_x = Position(server, kClientAId).x();
  client_a.EmitInput(KeyDown(z13::fbs::input::Keycode::KEY_W));
  ASSERT_TRUE(RunNetworkUntil(*network, {server, client_a}, kNetTestDeltaTime, kMaxNetTestTicks, [&] {
    return Position(server, kClientAId).x() - spawn_x > 0.01f;
  })) << "the held command never took effect on the server";

  Z13TestWorld client_b = MakeClient(network);
  ASSERT_TRUE(RunNetworkUntil(*network, {server, client_a, client_b}, kNetTestDeltaTime, kMaxNetTestTicks, [&] {
    return IsConnected(client_b);
  }));

  const float position_at_join = Position(client_b, kClientAId).x();

  constexpr int kExtraTicks = 20;
  RunNetworkUntil(*network, {server, client_a, client_b}, kNetTestDeltaTime, kExtraTicks, [] { return false; });

  const float moved = Position(client_b, kClientAId).x() - position_at_join;
  const float expected = static_cast<float>(kExtraTicks) * z13::gameplay::kCameraVelocity * kNetTestDeltaTime;
  EXPECT_NEAR(moved, expected, z13::testing::kTestEpsilon)
      << "held_values didn't seed the joiner's view of an already-held key";
}

TEST(CommandStreamTest, LocalCommandMovesTheClientOnTheTickItIsPressed) {
  auto network = std::make_shared<InMemoryNetwork>();
  Z13TestWorld server = MakeServer(network);
  Z13TestWorld client_a = MakeClient(network);

  ASSERT_TRUE(RunNetworkUntil(
      *network, {server, client_a}, kNetTestDeltaTime, kMaxNetTestTicks, [&] { return IsConnected(client_a); }));

  const float before = Position(client_a, kClientAId).x();
  client_a.EmitInput(KeyDown(z13::fbs::input::Keycode::KEY_W));
  RunNetworkUntil(*network, {server, client_a}, kNetTestDeltaTime, 1, [] { return false; });

  const float expected = z13::gameplay::kCameraVelocity * kNetTestDeltaTime;
  EXPECT_NEAR(Position(client_a, kClientAId).x() - before, expected, z13::testing::kTestEpsilon)
      << "the client's own command waited instead of applying on the tick it was pressed";
}

TEST(CommandStreamTest, HeldDurationSurvivesJitteredDelivery) {
  auto network = std::make_shared<InMemoryNetwork>();
  network->SetFaultConfig({.drop_probability = 0., .min_delay_ticks = 0, .max_delay_ticks = 2});
  Z13TestWorld server = MakeServer(network);
  Z13TestWorld client_a = MakeClient(network);
  Z13TestWorld client_b = MakeClient(network);

  ASSERT_TRUE(RunNetworkUntil(*network, {server, client_a, client_b}, kNetTestDeltaTime, kMaxNetTestTicks, [&] {
    return IsConnected(client_a) && IsConnected(client_b);
  }));

  const float before = Position(client_b, kClientAId).x();

  constexpr uint64_t kHoldTicks = 20;
  constexpr uint64_t kSettleTicks = 30;
  client_a.EmitInput(KeyDown(z13::fbs::input::Keycode::KEY_W));
  RunNetworkUntil(*network, {server, client_a, client_b}, kNetTestDeltaTime, kHoldTicks, [] { return false; });
  client_a.EmitInput(KeyUp(z13::fbs::input::Keycode::KEY_W));
  RunNetworkUntil(*network, {server, client_a, client_b}, kNetTestDeltaTime, kSettleTicks, [] { return false; });

  const float moved = Position(client_b, kClientAId).x() - before;
  const float expected = static_cast<float>(kHoldTicks) * z13::gameplay::kCameraVelocity * kNetTestDeltaTime;
  EXPECT_NEAR(moved, expected, z13::testing::kTestEpsilon)
      << "jittered delivery changed how long the hold registered as lasting";
}

TEST(CommandStreamTest, SimultaneousBuildsFromTwoClientsLandOnBothWorlds) {
  auto network = std::make_shared<InMemoryNetwork>();
  network->SetFaultConfig({.drop_probability = 0., .min_delay_ticks = 0, .max_delay_ticks = 2});
  Z13TestWorld server = MakeServer(network);
  Z13TestWorld client_a = MakeClient(network);
  Z13TestWorld client_b = MakeClient(network);

  ASSERT_TRUE(RunNetworkUntil(*network, {server, client_a, client_b}, kNetTestDeltaTime, kMaxNetTestTicks, [&] {
    return IsConnected(client_a) && IsConnected(client_b);
  }));

  constexpr uint64_t kSettleTicks = 30;
  client_a.EmitInput(KeyDown(z13::fbs::input::Keycode::KEY_TAB));
  client_b.EmitInput(KeyDown(z13::fbs::input::Keycode::KEY_TAB));
  RunNetworkUntil(*network, {server, client_a, client_b}, kNetTestDeltaTime, 1, [] { return false; });
  client_a.EmitInput(KeyUp(z13::fbs::input::Keycode::KEY_TAB));
  client_b.EmitInput(KeyUp(z13::fbs::input::Keycode::KEY_TAB));
  RunNetworkUntil(*network, {server, client_a, client_b}, kNetTestDeltaTime, kSettleTicks, [] { return false; });

  client_a.EmitInput(MouseDown(z13::fbs::input::Keycode::MOUSE_BUTTON_LEFT));
  client_b.EmitInput(MouseDown(z13::fbs::input::Keycode::MOUSE_BUTTON_LEFT));
  RunNetworkUntil(*network, {server, client_a, client_b}, kNetTestDeltaTime, 1, [] { return false; });
  client_a.EmitInput(MouseUp(z13::fbs::input::Keycode::MOUSE_BUTTON_LEFT));
  client_b.EmitInput(MouseUp(z13::fbs::input::Keycode::MOUSE_BUTTON_LEFT));
  RunNetworkUntil(*network, {server, client_a, client_b}, kNetTestDeltaTime, kSettleTicks, [] { return false; });

  EXPECT_EQ(BlockCount(server.World()), 2u);
  EXPECT_EQ(BlockCount(client_a.World()), 2u);
  EXPECT_EQ(BlockCount(client_b.World()), 2u);
}

TEST(CommandStreamTest, HeldKeyReassertsPeriodicallyNotEveryTick) {
  auto network = std::make_shared<InMemoryNetwork>();
  Z13TestWorld server = MakeServer(network);
  Z13TestWorld client_a = MakeClient(network);

  ASSERT_TRUE(RunNetworkUntil(
      *network, {server, client_a}, kNetTestDeltaTime, kMaxNetTestTicks, [&] { return IsConnected(client_a); }));

  const uint64_t interval_ticks = SnapshotIntervalTicks(server);
  const uint64_t hold_ticks = interval_ticks * 2 + 10;  // spans two reassert boundaries
  client_a.EmitInput(KeyDown(z13::fbs::input::Keycode::KEY_W));
  RunNetworkUntil(*network, {server, client_a}, kNetTestDeltaTime, hold_ticks, [] { return false; });
  client_a.EmitInput(KeyUp(z13::fbs::input::Keycode::KEY_W));

  const size_t log_count = LogCountFor(server, kClientAId);
  EXPECT_GE(log_count, 1u);
  EXPECT_LE(log_count, 4u) << "expected roughly one record per press/reassert boundary, not one per tick";
}

}  // namespace
}  // namespace z13::net
