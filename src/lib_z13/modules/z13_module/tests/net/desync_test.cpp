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
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include <Eigen/Dense>

#include <lib_core/rollback.h>
#include <lib_core/simulation_clock.h>
#include <lib_core/world_json_store.h>
#include <lib_core/world_state.h>

#include <net_module/clock_sync.h>
#include <net_module/in_memory_transport.h>
#include <net_module/state_digest.h>

#include <z13/components/building.h>
#include <z13/components/gameplay.h>
#include <z13/components/net.h>
#include <z13/components/player_action.h>

#include "../support/building_test_helpers.h"
#include "../support/test_network.h"
#include "../support/world_json_test_helpers.h"
#include "../support/z13_test_world.h"

namespace z13::net {
namespace {

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
  return Z13TestWorld(/*skip_main_menu=*/false, {std::string(z13::testing::kServerArg)}, network);
}

Z13TestWorld MakeClient(const std::shared_ptr<InMemoryNetwork>& network) {
  return Z13TestWorld(
      /*skip_main_menu=*/false, {std::string(z13::testing::kConnectArg), std::string(kTestServerEndpoint)}, network);
}

bool IsConnected(Z13TestWorld& world) {
  return world.World().has<z13::gameplay::Gameplay>() &&
      world.World().get<ConnectionStatus>().state == ConnectionState::kConnected;
}

const StateDigests& Digests(Z13TestWorld& world) {
  return world.World().get<StateDigests>();
}

std::string Checkpoint(Z13TestWorld& test_world) {
  const auto json = ft::WorldJsonStore::Save(test_world.World());
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
  // Not EnterBuildMode/Click: those tick the client alone, off the network.
  client_.EmitInput(KeyDown(Keycode::KEY_TAB));
  Run(1, [] { return false; });
  client_.EmitInput(KeyUp(Keycode::KEY_TAB));
  Settle();
  client_.EmitInput(z13::testing::MouseDown(Keycode::MOUSE_BUTTON_LEFT));
  Run(1, [] { return false; });
  client_.EmitInput(z13::testing::MouseUp(Keycode::MOUSE_BUTTON_LEFT));
  Settle();
  ASSERT_EQ(server_.World().count<z13::building::BasicBlock>(), 1);

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
      .set(Eigen::Matrix4f(Eigen::Matrix4f::Identity()))
      .add<z13::building::BasicBlock>();

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

// Set and cleared by hand, with no ResyncRequest sent, so no Resync interferes.
TEST_F(DesyncTest, InputWhileAwaitingResyncIsHeldBackThenSentOnTime) {
  client_.World().get_mut<StateDigests>().awaiting_resync = true;
  client_.EmitInput(KeyDown(Keycode::KEY_W));
  Run(kSettleTicks, [] { return false; });

  const auto logged = [](Z13TestWorld& world) -> const auto& {
    return world.World().get<z13::gameplay::PlayerActionLog>().log.Entries();
  };
  EXPECT_TRUE(logged(server_).empty());
  EXPECT_TRUE(logged(client_).empty()) << "held-back input must not apply locally either";

  const uint64_t resumed_at = client_.World().get<ft::SimulationClock>().tick;
  client_.World().get_mut<StateDigests>().awaiting_resync = false;
  ASSERT_TRUE(Run(kMaxNetTestTicks, [&] { return !logged(server_).empty(); })) << "the held-back press was never sent";
  EXPECT_GT(logged(server_).front().tick, resumed_at) << "sent for the tick it was pressed, not the resume";

  client_.EmitInput(KeyUp(Keycode::KEY_W));
  ASSERT_TRUE(Run(kMaxNetTestTicks, [&] { return Digests(client_).checked >= 2; }));
  EXPECT_EQ(Digests(client_).resyncs, 0u);
  EXPECT_EQ(Checkpoint(server_), Checkpoint(client_));
}

// The Resync's log lacks input applied but not yet sent.
TEST_F(DesyncTest, InputAppliedButUnsentWhenAResyncStartsIsReappliedAfterIt) {
  const uint32_t local_id = *client_.World().get<z13::gameplay::LocalPlayer>().id;
  const auto client_tick = [&] { return client_.World().get<ft::SimulationClock>().tick; };
  // Right after a send tick, so the record waits.
  ASSERT_TRUE(Run(kMaxNetTestTicks, [&] { return client_tick() % kNetSendIntervalTicks == 0; }));
  client_.EmitInput(KeyDown(Keycode::KEY_W));
  Run(1, [] { return false; });
  ASSERT_EQ(client_.World().get<z13::gameplay::OutgoingCommands>().records.size(), 1u) << "sent already";

  client_.World().get_mut<StateDigests>().awaiting_resync = true;
  client_.World().get_mut<z13::gameplay::PlayerActionLog>().log.RemoveIf(
      [local_id](const auto& record) { return record.player_id == local_id; });
  Run(kSettleTicks, [] { return false; });
  client_.World().get_mut<StateDigests>().awaiting_resync = false;

  const auto logged_ticks = [local_id](Z13TestWorld& world) {
    std::vector<uint64_t> ticks;
    for (const auto& record : world.World().get<z13::gameplay::PlayerActionLog>().log.Entries()) {
      if (record.player_id == local_id) {
        ticks.push_back(record.tick);
      }
    }
    return ticks;
  };
  ASSERT_TRUE(Run(kMaxNetTestTicks, [&] { return !logged_ticks(server_).empty(); }));
  EXPECT_EQ(logged_ticks(client_), logged_ticks(server_));
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
