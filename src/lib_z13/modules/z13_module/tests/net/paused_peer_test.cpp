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
#include <functional>
#include <memory>
#include <string>

#include <Eigen/Dense>

#include <lib_core/time/simulation_clock.h>
#include <lib_core/utils/math.h>

#include <net_module/in_memory_transport.h>
#include <net_module/state_digest.h>

#include <z13/components/gameplay.h>
#include <z13/components/net.h>
#include <z13_module/gameplay/gameplay_entities.h>

#include "../support/building_test_helpers.h"
#include "../support/test_network.h"
#include "../support/z13_test_world.h"

// Pause in a networked session only neutralizes the local input; the simulation runs on.
namespace z13::net {
namespace {

using z13::testing::KeyDown;
using z13::testing::KeyUp;
using z13::testing::kConnectArg;
using z13::testing::kMaxNetTestTicks;
using z13::testing::kNetTestDeltaTime;
using z13::testing::kServerArg;
using z13::testing::kTestServerEndpoint;
using z13::testing::RunNetworkUntil;
using z13::testing::Z13TestWorld;

constexpr uint32_t kHostId = 0;
constexpr uint32_t kClientId = 1;
constexpr uint64_t kDigestChecks = 3;
constexpr uint64_t kSettleTicks = 30;

class PausedPeerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(Run(kMaxNetTestTicks, [&] {
      return client_.World().has<z13::gameplay::Gameplay>() &&
          client_.World().get<ConnectionStatus>().state == ConnectionState::kConnected;
    }));
  }

  bool Run(uint64_t max_ticks, const std::function<bool()>& condition) {
    return RunNetworkUntil(*network_, {server_, client_}, kNetTestDeltaTime, max_ticks, condition);
  }

  void RunTicks(uint64_t ticks) {
    Run(ticks, [] { return false; });
  }

  // A paused peer used to trigger a Resync at each check.
  bool RunDigestChecks() {
    const uint64_t target = client_.World().get<StateDigests>().checked + kDigestChecks;
    return Run(kMaxNetTestTicks, [&] { return client_.World().get<StateDigests>().checked >= target; });
  }

  static Eigen::Vector3f Position(Z13TestWorld& world, uint32_t player_id) {
    const flecs::entity player = world.World().lookup(z13::gameplay::PlayerEntityName(player_id).c_str());
    EXPECT_TRUE(player) << "no player " << player_id;
    return player ? z13::math::ExtractTranslation<float>(player.get<Eigen::Matrix4f>()) : Eigen::Vector3f::Zero();
  }

  // Peers' clocks differ by a tick, so compare only after the mover stops.
  void ReleaseAndSettle(Z13TestWorld& mover) {
    mover.EmitInput(KeyUp(z13::fbs::input::Keycode::KEY_W));
    RunTicks(kSettleTicks);
  }

  void ExpectSameView(uint32_t player_id) {
    EXPECT_LT((Position(server_, player_id) - Position(client_, player_id)).norm(), z13::testing::kTestEpsilon)
        << "server (" << Position(server_, player_id).transpose() << ") vs client ("
        << Position(client_, player_id).transpose() << ")";
  }

  std::shared_ptr<InMemoryNetwork> network_ = std::make_shared<InMemoryNetwork>();
  Z13TestWorld server_ {/*skip_main_menu=*/false, {std::string(kServerArg)}, network_};
  Z13TestWorld client_ {
      /*skip_main_menu=*/false, {std::string(kConnectArg), std::string(kTestServerEndpoint)}, network_};
};

TEST_F(PausedPeerTest, PausedHostKeepsSimulatingTheClient) {
  server_.World().add<z13::gameplay::Pause>();
  const Eigen::Vector3f start = Position(client_, kClientId);

  client_.EmitInput(KeyDown(z13::fbs::input::Keycode::KEY_W));
  ASSERT_TRUE(RunDigestChecks());
  ReleaseAndSettle(client_);

  EXPECT_FALSE(server_.World().has<z13::flecs_tools::SimulationFrozen>());
  EXPECT_GT((Position(client_, kClientId) - start).norm(), 1.f) << "the client never moved";
  ExpectSameView(kClientId);
  EXPECT_EQ(client_.World().get<StateDigests>().resyncs, 0u) << "the paused host drifted from the client";
}

TEST_F(PausedPeerTest, PausedClientKeepsSimulatingTheHost) {
  client_.World().add<z13::gameplay::Pause>();
  const Eigen::Vector3f start = Position(server_, kHostId);

  server_.EmitInput(KeyDown(z13::fbs::input::Keycode::KEY_W));
  ASSERT_TRUE(RunDigestChecks());
  ReleaseAndSettle(server_);

  EXPECT_GT((Position(client_, kHostId) - start).norm(), 1.f) << "the paused client stopped moving the host";
  ExpectSameView(kHostId);
  EXPECT_EQ(client_.World().get<StateDigests>().resyncs, 0u);
}

TEST_F(PausedPeerTest, PausingWhileHoldingAKeyStopsThePlayerEverywhere) {
  client_.EmitInput(KeyDown(z13::fbs::input::Keycode::KEY_W));
  RunTicks(kSettleTicks);
  client_.World().add<z13::gameplay::Pause>();
  RunTicks(kSettleTicks);

  const Eigen::Vector3f on_client = Position(client_, kClientId);
  const Eigen::Vector3f on_server = Position(server_, kClientId);
  RunTicks(kSettleTicks);

  EXPECT_LT((Position(client_, kClientId) - on_client).norm(), z13::testing::kTestEpsilon)
      << "the held key kept driving the paused client";
  EXPECT_LT((Position(server_, kClientId) - on_server).norm(), z13::testing::kTestEpsilon)
      << "the release never reached the server";
  ExpectSameView(kClientId);
}

}  // namespace
}  // namespace z13::net
