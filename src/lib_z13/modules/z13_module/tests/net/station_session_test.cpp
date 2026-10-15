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

#include <chrono>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <string>

#include <Eigen/Dense>

#include <lib_core/utils/math.h>

#include <net_module/in_memory_transport.h>
#include <net_module/state_digest.h>

#include <z13/components/gameplay.h>
#include <z13/components/net.h>
#include <z13_module/gameplay/gameplay_entities.h>

#include "../support/building_test_helpers.h"
#include "../support/test_network.h"
#include "../support/z13_test_world.h"

// A client joining a host on the largest shipped station and playing there. Opt-in, it takes
// tens of seconds in Debug: --gtest_also_run_disabled_tests --gtest_filter=StationSessionTest.*
namespace z13::net {
namespace {

using z13::testing::KeyDown;
using z13::testing::KeyUp;
using z13::testing::kConnectArg;
using z13::testing::kNetTestDeltaTime;
using z13::testing::kServerArg;
using z13::testing::kStationSceneArg;
using z13::testing::kTestServerEndpoint;
using z13::testing::RunNetworkUntil;
using z13::testing::Z13TestWorld;
using Keycode = z13::fbs::input::Keycode;

constexpr uint32_t kClientId = 1;
constexpr uint64_t kMaxJoinTicks = 1200;
// One second of each action at 60 Hz.
constexpr uint64_t kActionTicks = 60;
constexpr uint64_t kSettleTicks = 30;
constexpr uint64_t kDigestChecks = 3;

class StationSessionTest : public ::testing::Test {
 protected:
  bool Run(uint64_t max_ticks, const std::function<bool()>& condition) {
    return RunNetworkUntil(*network_, {server_, client_}, kNetTestDeltaTime, max_ticks, condition);
  }

  void RunTicks(uint64_t ticks) {
    Run(ticks, [] { return false; });
  }

  void Hold(Keycode key) {
    client_.EmitInput(KeyDown(key));
    RunTicks(kActionTicks);
    client_.EmitInput(KeyUp(key));
    RunTicks(kSettleTicks);
  }

  static Eigen::Vector3f Position(Z13TestWorld& world, uint32_t player_id) {
    const flecs::entity player = world.World().lookup(z13::gameplay::PlayerEntityName(player_id).c_str());
    EXPECT_TRUE(player) << "no player " << player_id;
    return player ? z13::math::ExtractTranslation<float>(player.get<Eigen::Matrix4f>()) : Eigen::Vector3f::Zero();
  }

  std::shared_ptr<InMemoryNetwork> network_ = std::make_shared<InMemoryNetwork>();
  Z13TestWorld server_ {
      {std::string(kServerArg), std::string(kStationSceneArg), std::string(z13::testing::kSpireScene)}, network_};
  Z13TestWorld client_ {{std::string(kConnectArg), std::string(kTestServerEndpoint)}, network_};
};

TEST_F(StationSessionTest, DISABLED_AClientJoinsAndPlaysOnSpire) {
  const auto join_start = std::chrono::steady_clock::now();
  ASSERT_TRUE(Run(kMaxJoinTicks, [&] {
    return client_.World().has<z13::gameplay::Gameplay>() &&
           client_.World().get<ConnectionStatus>().state == ConnectionState::kConnected;
  }));
  const auto joined = std::chrono::steady_clock::now();
  std::cout << "joined in " << std::chrono::duration<double>(joined - join_start).count() << " s\n";

  const Eigen::Vector3f start = Position(client_, kClientId);
  for (const Keycode key : {Keycode::KEY_W, Keycode::KEY_A, Keycode::KEY_S, Keycode::KEY_D, Keycode::KEY_SPACE}) {
    Hold(key);
  }
  const uint64_t target = client_.World().get<StateDigests>().checked + kDigestChecks;
  ASSERT_TRUE(Run(kMaxJoinTicks, [&] { return client_.World().get<StateDigests>().checked >= target; }));
  std::cout << "played in " << std::chrono::duration<double>(std::chrono::steady_clock::now() - joined).count()
            << " s\n";

  EXPECT_TRUE(client_.World().has<z13::gameplay::Gameplay>()) << "the client left the game";
  EXPECT_EQ(client_.World().get<ConnectionStatus>().state, ConnectionState::kConnected);
  EXPECT_EQ(client_.World().get<StateDigests>().resyncs, 0u);
  EXPECT_GT((Position(client_, kClientId) - start).norm(), 0.f) << "the client never moved";
  EXPECT_LT((Position(server_, kClientId) - Position(client_, kClientId)).norm(), z13::testing::kTestEpsilon)
      << "the host sees the client elsewhere";
}

}  // namespace
}  // namespace z13::net
