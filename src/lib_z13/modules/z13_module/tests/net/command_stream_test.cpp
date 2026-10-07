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

#include <lib_core/state/rollback.h>
#include <lib_core/time/simulation_clock.h>
#include <lib_core/utils/math.h>

#include <net_module/clock_sync.h>
#include <net_module/in_memory_transport.h>
#include <net_module/state_digest.h>

#include <z13/components/building.h>
#include <z13/components/gameplay.h>
#include <z13/components/net.h>
#include <z13/components/player_action.h>
#include <z13/components/station.h>
#include <z13_module/gameplay/camera_look.h>
#include <z13_module/gameplay/gameplay_entities.h>
#include <z13_settings/settings.h>

#include "../support/building_test_helpers.h"
#include "../support/test_network.h"
#include "../support/z13_test_world.h"

namespace z13::net {
namespace {

const NetTuning kTuning;
const uint64_t kRollbackSnapshotsPerInterval = kTuning.rollback_snapshots_per_interval;
const uint64_t kNetSendIntervalTicks = kTuning.send_interval_ticks;
const uint64_t kMaxRollbackDelayTicks = CoreSettings {}.max_rollback_delay_ticks;

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

Z13TestWorld MakeServer(
    const std::shared_ptr<InMemoryNetwork>& network, const z13::Settings& settings = z13::MakeSettings()) {
  return Z13TestWorld({std::string(kServerArg)}, network, settings);
}

Z13TestWorld MakeClient(
    const std::shared_ptr<InMemoryNetwork>& network, const z13::Settings& settings = z13::MakeSettings()) {
  return Z13TestWorld({std::string(kConnectArg), std::string(kTestServerEndpoint)}, network, settings);
}

bool IsConnected(Z13TestWorld& world) {return world.World().has<z13::gameplay::Gameplay>() &&
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

size_t LogCountFor(Z13TestWorld& world, uint32_t player_id) {return static_cast<size_t>(std::ranges::count_if(
      world.World().get<z13::gameplay::PlayerActionLog>().log.Entries(),
      [player_id](const z13::gameplay::PlayerActionRecord& r) { return r.player_id == player_id; }));
}

size_t BlockCount(flecs::world world) {
  size_t count = 0;
  world.query_builder<const z13::station::Block>().build().each(
      [&](const z13::station::Block&) { ++count; });
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

// Welcome's snapshot is the newest one past the late-command window. B joins so that it was
// taken mid-hold and the held key's next reassert is far off: only held_values tell B W is down.
TEST(CommandStreamTest, LateJoinMidHoldSeesTheHeldMovement) {
  constexpr double kSnapshotIntervalSeconds = 5.;
  constexpr double kSnapshotRetentionSeconds = 10.;
  auto network = std::make_shared<InMemoryNetwork>();
  z13::Settings settings = z13::MakeSettings();
  settings.core->snapshot_interval_seconds = kSnapshotIntervalSeconds;
  settings.core->snapshot_retention_seconds = kSnapshotRetentionSeconds;
  Z13TestWorld server = MakeServer(network, settings);
  Z13TestWorld client_a = MakeClient(network, settings);

  ASSERT_TRUE(RunNetworkUntil(
      *network, {server, client_a}, kNetTestDeltaTime, kMaxNetTestTicks, [&] { return IsConnected(client_a); }));

  const float spawn_x = Position(server, kClientAId).x();
  client_a.EmitInput(KeyDown(z13::fbs::input::Keycode::KEY_W));
  ASSERT_TRUE(RunNetworkUntil(*network, {server, client_a}, kNetTestDeltaTime, kMaxNetTestTicks, [&] {
    return Position(server, kClientAId).x() - spawn_x > 0.01f;
  })) << "the held command never took effect on the server";

  const uint64_t interval_ticks = SnapshotIntervalTicks(server);
  const uint64_t join_offset = kTuning.max_late_ticks + 2;
  const uint64_t pressed_by = server.World().get<ft::SimulationClock>().tick;
  ASSERT_TRUE(RunNetworkUntil(*network, {server, client_a}, kNetTestDeltaTime, 3 * interval_ticks, [&] {
    const uint64_t tick = server.World().get<ft::SimulationClock>().tick;
    return tick > pressed_by + interval_ticks + join_offset && tick % interval_ticks == join_offset;
  }));

  Z13TestWorld client_b = MakeClient(network, settings);
  ASSERT_TRUE(RunNetworkUntil(*network, {server, client_a, client_b}, kNetTestDeltaTime, kMaxNetTestTicks, [&] {
    return IsConnected(client_b);
  }));

  const float position_at_join = Position(client_b, kClientAId).x();

  constexpr int kExtraTicks = 20;
  constexpr int kSettleTicks = 30;
  RunNetworkUntil(*network, {server, client_a, client_b}, kNetTestDeltaTime, kExtraTicks, [] { return false; });
  EXPECT_GT(Position(client_b, kClientAId).x(), position_at_join + z13::testing::kTestEpsilon)
      << "B doesn't see A move while the key is still held";
  client_a.EmitInput(KeyUp(z13::fbs::input::Keycode::KEY_W));
  RunNetworkUntil(*network, {server, client_a, client_b}, kNetTestDeltaTime, kSettleTicks, [] { return false; });

  EXPECT_NEAR(Position(client_b, kClientAId).x(), Position(server, kClientAId).x(), z13::testing::kTestEpsilon)
      << "held_values didn't seed the joiner's view of an already-held key";
  EXPECT_EQ(client_b.World().get<StateDigests>().resyncs, 0u) << "only a resync caught B up";
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

TEST(CommandStreamTest, LateCommandsRollBackInBatchesFromANearbySnapshot) {
  auto network = std::make_shared<InMemoryNetwork>();
  constexpr uint64_t kMaxDelayTicks = 4;
  network->SetFaultConfig({.drop_probability = 0., .min_delay_ticks = 2, .max_delay_ticks = kMaxDelayTicks});
  Z13TestWorld server = MakeServer(network);
  Z13TestWorld client_a = MakeClient(network);
  Z13TestWorld client_b = MakeClient(network);
  ASSERT_TRUE(RunNetworkUntil(*network, {server, client_a, client_b}, kNetTestDeltaTime, kMaxNetTestTicks, [&] {
    return IsConnected(client_a) && IsConnected(client_b);
  }));
  RunNetworkUntil(*network, {server, client_a, client_b}, kNetTestDeltaTime, SnapshotIntervalTicks(server), [] {
    return false;
  });
  const uint64_t rollbacks_before = server.World().get<ft::RollbackMetrics>().rollbacks;
  server.World().get_mut<ft::RollbackMetrics>().max_depth_ticks = 0;

  constexpr uint64_t kTapTicks = 120;
  for (uint64_t tick = 0; tick < kTapTicks; ++tick) {
    for (Z13TestWorld* client : {&client_a, &client_b}) {if (tick % 2 == 0) {
        client->EmitInput(KeyDown(z13::fbs::input::Keycode::KEY_W));
      } else {
        client->EmitInput(KeyUp(z13::fbs::input::Keycode::KEY_W));
      }
    }
    RunNetworkUntil(*network, {server, client_a, client_b}, kNetTestDeltaTime, 1, [] { return false; });
  }

  const auto& metrics = server.World().get<ft::RollbackMetrics>();
  EXPECT_LE(metrics.rollbacks - rollbacks_before, kTapTicks / kMaxRollbackDelayTicks + 1);
  const uint64_t capture_gap = SnapshotIntervalTicks(server) / kRollbackSnapshotsPerInterval;
  EXPECT_LE(metrics.max_depth_ticks, capture_gap + kMaxRollbackDelayTicks + kNetSendIntervalTicks + 2 * kMaxDelayTicks);
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

// Other players' late commands roll the client back; its own camera must not jump.
TEST(CommandStreamTest, OwnCameraMovesSmoothlyThroughRollbacks) {
  auto network = std::make_shared<InMemoryNetwork>();
  network->SetFaultConfig({.drop_probability = 0., .min_delay_ticks = 2, .max_delay_ticks = 4});
  Z13TestWorld server = MakeServer(network);
  Z13TestWorld client_a = MakeClient(network);
  Z13TestWorld client_b = MakeClient(network);
  ASSERT_TRUE(RunNetworkUntil(*network, {server, client_a, client_b}, kNetTestDeltaTime, kMaxNetTestTicks, [&] {
    return IsConnected(client_a) && IsConnected(client_b);
  }));

  // Delivery order under jitter decides which client gets which id.
  const uint32_t own_id = *client_a.World().get<z13::gameplay::LocalPlayer>().id;
  const uint64_t rollbacks_before = client_a.World().get<ft::RollbackMetrics>().rollbacks;
  constexpr uint64_t kTicks = 240;
  constexpr int kLookDelta = 7;
  const float max_step = z13::gameplay::kCameraVelocity * kNetTestDeltaTime + z13::testing::kTestEpsilon;
  for (Z13TestWorld* client : {&client_a, &client_b}) {client->EmitInput(KeyDown(z13::fbs::input::Keycode::KEY_W));
  }
  Eigen::Vector3f previous = Position(client_a, own_id);
  for (uint64_t tick = 0; tick < kTicks; ++tick) {
    z13::input::MouseMoveEvent look;
    look.delta = {.x = kLookDelta, .y = 0};
    client_a.EmitInput(look);
    if (tick % 3 == 0) {
      client_b.EmitInput(look);
    }
    const uint64_t clock_before = client_a.World().get<ft::SimulationClock>().tick;
    RunNetworkUntil(*network, {server, client_a, client_b}, kNetTestDeltaTime, 1, [] { return false; });
    // A clock catch-up runs an extra tick in the frame; only rollbacks must not move the camera.
    const uint64_t ticks_run = client_a.World().get<ft::SimulationClock>().tick - clock_before;
    const Eigen::Vector3f current = Position(client_a, own_id);
    EXPECT_LE((current - previous).norm(), max_step * static_cast<float>(std::max<uint64_t>(ticks_run, 1)))
        << "own camera jumped at tick " << tick << ": (" << previous.transpose() << ") -> (" << current.transpose()
        << ")";
    previous = current;
  }
  EXPECT_GT(client_a.World().get<ft::RollbackMetrics>().rollbacks, rollbacks_before) << "no rollbacks exercised";
}

float Pitch(Z13TestWorld& world, uint32_t player_id) {
  const flecs::entity player = world.World().lookup(z13::gameplay::PlayerEntityName(player_id).c_str());
  EXPECT_TRUE(player) << "no player " << player_id;
  return player ? player.get<z13::gameplay::LookAngles>().pitch_deg : 0.f;
}

z13::input::MouseMoveEvent LookVertically(int delta) {
  z13::input::MouseMoveEvent look;
  look.delta = {.x = 0, .y = delta};
  return look;
}

// An observer may lag a remote player's look, never overshoot it.
TEST(CommandStreamTest, ObserverNeverOvershootsARemoteLook) {
  auto network = std::make_shared<InMemoryNetwork>();
  network->SetFaultConfig({.drop_probability = 0., .min_delay_ticks = 2, .max_delay_ticks = 4});
  Z13TestWorld server = MakeServer(network);
  Z13TestWorld client_a = MakeClient(network);
  Z13TestWorld client_b = MakeClient(network);
  ASSERT_TRUE(RunNetworkUntil(*network, {server, client_a, client_b}, kNetTestDeltaTime, kMaxNetTestTicks, [&] {
    return IsConnected(client_a) && IsConnected(client_b);
  }));

  const uint32_t a_id = *client_a.World().get<z13::gameplay::LocalPlayer>().id;
  constexpr int kSweepTicks = 30;
  constexpr int kSweeps = 4;
  constexpr int kStillTicks = 40;
  constexpr int kLookDelta = 6;
  float lowest = Pitch(client_a, a_id);
  float highest = lowest;
  for (int tick = 0; tick < kSweepTicks * kSweeps + kStillTicks; ++tick) {
    if (tick < kSweepTicks * kSweeps) {
      client_a.EmitInput(LookVertically((tick / kSweepTicks) % 2 == 0 ? kLookDelta : -kLookDelta));
    }
    RunNetworkUntil(*network, {server, client_a, client_b}, kNetTestDeltaTime, 1, [] { return false; });
    lowest = std::min(lowest, Pitch(client_a, a_id));
    highest = std::max(highest, Pitch(client_a, a_id));
    const float seen = Pitch(client_b, a_id);
    EXPECT_GE(seen, lowest - z13::testing::kTestEpsilon) << "overshoot at tick " << tick;
    EXPECT_LE(seen, highest + z13::testing::kTestEpsilon) << "overshoot at tick " << tick;
  }
  EXPECT_NE(highest, lowest) << "the look never moved";
  EXPECT_NEAR(Pitch(client_b, a_id), Pitch(client_a, a_id), z13::testing::kTestEpsilon);
}

// A look value has no neutral 0: pausing must keep the angle, not turn the player to 0.
TEST(CommandStreamTest, PausingKeepsTheLookAngle) {
  auto network = std::make_shared<InMemoryNetwork>();
  Z13TestWorld server = MakeServer(network);
  Z13TestWorld client_a = MakeClient(network);
  ASSERT_TRUE(RunNetworkUntil(
      *network, {server, client_a}, kNetTestDeltaTime, kMaxNetTestTicks, [&] { return IsConnected(client_a); }));
  const uint32_t a_id = *client_a.World().get<z13::gameplay::LocalPlayer>().id;

  constexpr int kLookTicks = 10;
  constexpr int kLookDelta = 6;
  constexpr int kPausedTicks = 30;
  for (int tick = 0; tick < kLookTicks; ++tick) {
    client_a.EmitInput(LookVertically(kLookDelta));
    RunNetworkUntil(*network, {server, client_a}, kNetTestDeltaTime, 1, [] { return false; });
  }
  const float looked = Pitch(client_a, a_id);
  ASSERT_NE(looked, 0.f);

  client_a.World().add<z13::gameplay::Pause>();
  RunNetworkUntil(*network, {server, client_a}, kNetTestDeltaTime, kPausedTicks, [] { return false; });

  EXPECT_FLOAT_EQ(Pitch(client_a, a_id), looked);
  EXPECT_FLOAT_EQ(Pitch(server, a_id), looked);
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
