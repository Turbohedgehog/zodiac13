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
#include <functional>
#include <iterator>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include <Eigen/Dense>

#include <lib_core/state/rollback.h>
#include <lib_core/state/world_json_store.h>
#include <lib_core/state/world_state.h>
#include <lib_core/time/simulation_clock.h>

#include <net_module/clock_sync.h>
#include <net_module/in_memory_transport.h>
#include <net_module/state_digest.h>

#include <z13/components/building.h>
#include <z13/components/gameplay.h>
#include <z13/components/net.h>
#include <z13/components/player_action.h>
#include <z13/components/station.h>

#include "../support/block_test_helpers.h"
#include "../support/building_test_helpers.h"
#include "../support/test_network.h"
#include "../support/world_json_test_helpers.h"
#include "../support/z13_test_world.h"

namespace z13::net {
namespace {

const uint64_t kNetSendIntervalTicks = NetTuning {}.send_interval_ticks;

namespace ft = z13::flecs_tools;
using z13::testing::KeyDown;
using z13::testing::KeyUp;
using z13::testing::kMaxNetTestTicks;
using z13::testing::kNetTestDeltaTime;
using z13::testing::kTestServerEndpoint;
using z13::testing::RunNetworkUntil;
using z13::testing::Z13TestWorld;
using Keycode = z13::fbs::input::Keycode;

constexpr uint64_t kSettleTicks = 30;

Z13TestWorld MakeServer(const std::shared_ptr<InMemoryNetwork>& network) {
  return Z13TestWorld({std::string(z13::testing::kServerArg)}, network);
}

Z13TestWorld MakeClient(const std::shared_ptr<InMemoryNetwork>& network) {return Z13TestWorld(
      {std::string(z13::testing::kConnectArg), std::string(kTestServerEndpoint)}, network);
}

bool IsConnected(Z13TestWorld& world) {return world.World().has<z13::gameplay::Gameplay>() &&
      world.World().get<ConnectionStatus>().state == ConnectionState::kConnected;
}

const StateDigests& Digests(Z13TestWorld& world) {return world.World().get<StateDigests>();
}

std::string Checkpoint(Z13TestWorld& test_world) {const auto json = ft::WorldJsonStore::Save(test_world.World());
  EXPECT_TRUE(json.has_value()) << (json ? "" : json.error());
  return z13::testing::WithNormalizedSimulationTick(json.value_or(""));
}

size_t PlayerCount(flecs::world world) {
  return static_cast<size_t>(world.count<z13::gameplay::Player>());
}

bool HasDuplicateRecords(flecs::world world) {
  const auto& entries = world.get<z13::gameplay::PlayerActionLog>().log.Entries();
  return std::ranges::adjacent_find(entries, [](const auto& a, const auto& b) {
    return std::tie(a.tick, a.player_id, a.action_id) == std::tie(b.tick, b.player_id, b.action_id);
  }) != entries.end();
}

class DesyncTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(Run(kMaxNetTestTicks, [&] { return IsConnected(client_); }));
  }

  bool Run(uint64_t max_ticks, const std::function<bool()>& condition) {
    return RunNetworkUntil(*network_, {server_, client_}, kNetTestDeltaTime, max_ticks, condition);
  }

  void Settle() {
    Run(kSettleTicks, [] { return false; });
  }

  // Runs until the resync has landed and a later digest has confirmed it.
  bool RunUntilResynced() {
    return Run(kMaxNetTestTicks, [&] { return Digests(client_).resyncs > 0; }) &&
        Run(kMaxNetTestTicks, [&, checked = Digests(client_).checked] { return Digests(client_).checked > checked; });
  }

  std::shared_ptr<InMemoryNetwork> network_ = std::make_shared<InMemoryNetwork>();
  Z13TestWorld server_ = MakeServer(network_);
  Z13TestWorld client_ = MakeClient(network_);
};

TEST_F(DesyncTest, DigestsAgreeAfterAScriptedSession) {
  client_.EmitInput(KeyDown(Keycode::KEY_W));
  Run(20, [] { return false; });
  client_.EmitInput(KeyUp(Keycode::KEY_W));
  const auto step = [this] { Run(1, [] { return false; }); };
  z13::testing::Tap(client_, Keycode::KEY_TAB, step);
  Settle();
  z13::testing::Click(client_, Keycode::MOUSE_BUTTON_LEFT, step);
  Settle();
  ASSERT_EQ(server_.World().count<z13::station::Block>(), 1);

  ASSERT_TRUE(Run(kMaxNetTestTicks, [&] { return Digests(client_).checked >= 3; }));
  EXPECT_EQ(Digests(client_).resyncs, 0u);
  EXPECT_FALSE(Digests(client_).awaiting_resync);
  EXPECT_EQ(Checkpoint(server_), Checkpoint(client_));
}

TEST_F(DesyncTest, ExtraEntityOnTheClientIsDetectedAndResynced) {
  const auto local_id = client_.World().get<z13::gameplay::LocalPlayer>().id;
  client_.World()
      .entity("Block_Stray")
      .add<ft::StateEntity>()
      .set(z13::testing::CubeAt(Eigen::Vector3f::Zero()));

  ASSERT_TRUE(RunUntilResynced());
  Settle();

  EXPECT_FALSE(client_.World().lookup("Block_Stray"));
  EXPECT_EQ(client_.World().get<z13::gameplay::LocalPlayer>().id, local_id);
  EXPECT_TRUE(client_.World().has<z13::gameplay::Gameplay>());
  EXPECT_TRUE(client_.Player()) << "the local player lost its input after the resync";
  EXPECT_EQ(PlayerCount(client_.World()), PlayerCount(server_.World()));
  EXPECT_EQ(Checkpoint(server_), Checkpoint(client_));
}

// The client's history loses its own hold after the server has applied it.
TEST_F(DesyncTest, ForgottenCommandIsDetectedAndResynced) {
  const uint32_t local_id = *client_.World().get<z13::gameplay::LocalPlayer>().id;
  const uint64_t before_press = client_.World().get<ft::SimulationClock>().tick;

  client_.EmitInput(KeyDown(Keycode::KEY_W));
  Run(20, [] { return false; });
  client_.EmitInput(KeyUp(Keycode::KEY_W));
  Run(5, [] { return false; });
  client_.World().get_mut<z13::gameplay::PlayerActionLog>().log.RemoveIf(
      [local_id](const auto& record) { return record.player_id == local_id; });
  ft::RequestRollback(client_.World(), before_press, client_.World().get<ft::SimulationClock>().tick);

  ASSERT_TRUE(RunUntilResynced());
  Settle();

  EXPECT_EQ(Checkpoint(server_), Checkpoint(client_));
}

TEST_F(DesyncTest, FailedRollbackResyncsInsteadOfClosingTheSession) {
  ft::RequestRollback(client_.World(), 0, client_.World().get<ft::SimulationClock>().tick);

  ASSERT_TRUE(RunUntilResynced());
  Settle();

  EXPECT_TRUE(IsConnected(client_));
  EXPECT_EQ(Checkpoint(server_), Checkpoint(client_));
}

// Recorded before the ResyncRequest but sent after it, so the Resync lacks it.
TEST_F(DesyncTest, ReleaseUnsentWhenAResyncStartsSurvivesIt) {
  const uint32_t local_id = *client_.World().get<z13::gameplay::LocalPlayer>().id;
  const auto client_tick = [&] { return client_.World().get<ft::SimulationClock>().tick; };
  const auto own_records = [local_id](Z13TestWorld& world) {
    std::vector<z13::gameplay::PlayerActionRecord> records;
    std::ranges::copy_if(
        world.World().get<z13::gameplay::PlayerActionLog>().log.Entries(), std::back_inserter(records),
        [local_id](const auto& record) { return record.player_id == local_id; });
    return records;
  };
  client_.EmitInput(KeyDown(Keycode::KEY_W));
  ASSERT_TRUE(Run(kMaxNetTestTicks, [&] { return !own_records(server_).empty(); }));

  // Right after a send tick, so the release is recorded before the ResyncRequest and not sent.
  ASSERT_TRUE(Run(kMaxNetTestTicks, [&] { return client_tick() % kNetSendIntervalTicks == 0; }));
  client_.EmitInput(KeyUp(Keycode::KEY_W));
  ft::RequestRollback(client_.World(), 0, client_tick());
  ASSERT_TRUE(Run(kMaxNetTestTicks, [&] { return Digests(client_).awaiting_resync; }));
  ASSERT_EQ(client_.World().get<z13::gameplay::OutgoingCommands>().records.size(), 1u) << "sent already";

  ASSERT_TRUE(RunUntilResynced());
  ASSERT_TRUE(Run(kMaxNetTestTicks, [&] { return own_records(server_).back().value == 0.f; }))
      << "the server still holds the released key";
  ASSERT_TRUE(Run(kMaxNetTestTicks, [&, checked = Digests(client_).checked] {
    return Digests(client_).checked >= checked + 2;
  }));
  EXPECT_EQ(Digests(client_).resyncs, 1u);
  EXPECT_EQ(Checkpoint(server_), Checkpoint(client_));
}

// A press and its release, both within the wait: neither may be lost or merged into the other.
TEST_F(DesyncTest, EveryInputWhileAwaitingAResyncIsApplied) {
  const auto latency_ticks = static_cast<uint32_t>(2 * kNetSendIntervalTicks);
  network_->SetFaultConfig({.min_delay_ticks = latency_ticks, .max_delay_ticks = latency_ticks});
  Settle();
  const uint32_t local_id = *client_.World().get<z13::gameplay::LocalPlayer>().id;
  const auto own_values = [local_id](Z13TestWorld& world) {
    std::vector<float> values;
    for (const auto& record : world.World().get<z13::gameplay::PlayerActionLog>().log.Entries()) {
      if (record.player_id == local_id && record.value != 0.f) {
        values.push_back(record.value);
      }
    }
    return values;
  };
  client_.World()
      .entity("Block_Stray")
      .add<ft::StateEntity>()
      .set(z13::testing::CubeAt(Eigen::Vector3f::Zero()));
  ASSERT_TRUE(Run(kMaxNetTestTicks, [&] { return Digests(client_).awaiting_resync; }));

  client_.EmitInput(KeyDown(Keycode::KEY_W));
  Run(1, [] { return false; });
  client_.EmitInput(KeyUp(Keycode::KEY_W));
  Run(1, [] { return false; });
  ASSERT_TRUE(Digests(client_).awaiting_resync) << "the Resync came back before the tap ended";

  ASSERT_TRUE(RunUntilResynced());
  Settle();
  EXPECT_EQ(Digests(client_).resyncs, 1u);
  EXPECT_EQ(own_values(server_).size(), 1u) << "the press never reached the server";
  EXPECT_EQ(own_values(client_), own_values(server_));
  EXPECT_EQ(Checkpoint(server_), Checkpoint(client_));
}

// A real ResyncRequest over a slow link: heartbeats sent after it reach the server before the
// Resync reaches the client, and must not break what the Resync restores.
TEST_F(DesyncTest, HeartbeatsWhileAResyncIsInFlightKeepConfirmingInput) {
  const auto latency_ticks = static_cast<uint32_t>(2 * kNetSendIntervalTicks);
  network_->SetFaultConfig({.min_delay_ticks = latency_ticks, .max_delay_ticks = latency_ticks});
  Settle();
  const uint32_t local_id = *client_.World().get<z13::gameplay::LocalPlayer>().id;
  const auto confirmed_on_server = [&]() -> uint64_t {
    const auto& by_player = server_.World().get<z13::gameplay::ConfirmedInputTicks>().by_player;
    const auto entry = by_player.find(local_id);
    return entry == by_player.end() ? 0 : entry->second;
  };
  client_.World()
      .entity("Block_Stray")
      .add<ft::StateEntity>()
      .set(z13::testing::CubeAt(Eigen::Vector3f::Zero()));

  ASSERT_TRUE(Run(kMaxNetTestTicks, [&] { return Digests(client_).awaiting_resync; }));
  const uint64_t requested_at = client_.World().get<ft::SimulationClock>().tick;
  client_.EmitInput(KeyDown(Keycode::KEY_W));
  ASSERT_TRUE(Run(kMaxNetTestTicks, [&] { return confirmed_on_server() > requested_at; }));
  EXPECT_TRUE(Digests(client_).awaiting_resync) << "no heartbeat reached the server before the Resync came back";

  ASSERT_TRUE(RunUntilResynced());
  client_.EmitInput(KeyUp(Keycode::KEY_W));
  ASSERT_TRUE(Run(kMaxNetTestTicks, [&, checked = Digests(client_).checked] {
    return Digests(client_).checked >= checked + 2;
  }));

  EXPECT_FALSE(client_.World().lookup("Block_Stray"));
  EXPECT_EQ(Digests(client_).resyncs, 1u);
  const auto& entries = server_.World().get<z13::gameplay::PlayerActionLog>().log.Entries();
  EXPECT_TRUE(std::ranges::any_of(entries, [local_id](const auto& record) { return record.player_id == local_id; }))
      << "the press held back during the wait never reached the server";
  EXPECT_EQ(Checkpoint(server_), Checkpoint(client_));
}

TEST_F(DesyncTest, ReplayNeitherResendsNorRerecordsCommands) {
  constexpr uint64_t kRollbacks = 3;
  constexpr uint64_t kRollbackDepthTicks = 10;
  const uint64_t rollbacks_before = client_.World().get<ft::RollbackMetrics>().rollbacks;

  client_.EmitInput(KeyDown(Keycode::KEY_W));
  for (uint64_t i = 0; i < kRollbacks; ++i) {
    Run(kSettleTicks, [] { return false; });
    const uint64_t now = client_.World().get<ft::SimulationClock>().tick;
    ft::RequestRollback(client_.World(), now - kRollbackDepthTicks, now);
  }
  client_.EmitInput(KeyUp(Keycode::KEY_W));
  ASSERT_TRUE(Run(kMaxNetTestTicks, [&] { return Digests(client_).checked >= 2; }));

  const auto& metrics = client_.World().get<ft::RollbackMetrics>();
  EXPECT_GE(metrics.rollbacks, rollbacks_before + kRollbacks);
  EXPECT_GE(metrics.max_depth_ticks, kRollbackDepthTicks);
  EXPECT_FALSE(HasDuplicateRecords(server_.World())) << "a replayed tick resent its commands";
  EXPECT_FALSE(HasDuplicateRecords(client_.World())) << "a replayed tick re-recorded its commands";
  EXPECT_EQ(Digests(client_).resyncs, 0u);
  EXPECT_EQ(Checkpoint(server_), Checkpoint(client_));
}

}  // namespace
}  // namespace z13::net
