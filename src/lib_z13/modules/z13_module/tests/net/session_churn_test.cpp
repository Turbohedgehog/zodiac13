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
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Eigen/Dense>

#include <lib_core/settings/config.h>
#include <lib_core/state/state_markers.h>
#include <lib_core/utils/math.h>

#include <net_module/in_memory_transport.h>
#include <net_module/state_digest.h>

#include <z13/components/gameplay.h>
#include <z13/components/inventory.h>
#include <z13/components/net.h>
#include <z13/components/player.h>
#include <z13_module/gameplay/gameplay_entities.h>
#include <z13_tests/shipped_station.h>

#include "../support/building_test_helpers.h"
#include "../support/test_network.h"
#include "../support/z13_test_world.h"

// Players join, leave, drop and come back while others walk, over a jittery link; afterwards
// every participant holds the server's state and none needed a resync.
namespace z13::net {
namespace {

using z13::testing::KeyDown;
using z13::testing::KeyUp;
using z13::testing::kConnectArg;
using z13::testing::kNetTestDeltaTime;
using z13::testing::kServerArg;
using z13::testing::kStationSceneArg;
using z13::testing::kTestServerEndpoint;
using z13::testing::Z13TestWorld;
using Keycode = z13::fbs::input::Keycode;

// Commands arrive late and out of step with ticks, as over a real network.
constexpr FaultConfig kJitter {.min_delay_ticks = 2, .max_delay_ticks = 10};
constexpr uint64_t kMaxJoinTicks = 1200;
constexpr uint64_t kWalkTicks = 45;
// Digests checked after the churn: the first may still describe a tick before it settled.
constexpr uint64_t kDigestChecks = 2;
constexpr uint64_t kMaxSettleTicks = 1200;

class SessionChurnTest : public ::testing::TestWithParam<std::string_view> {
 protected:
  SessionChurnTest() { network_->SetFaultConfig(kJitter); }

  Z13TestWorld& Join() {
    clients_.push_back(std::make_unique<Z13TestWorld>(
        std::vector {std::string(kConnectArg), std::string(kTestServerEndpoint)}, network_));
    Z13TestWorld& client = *clients_.back();
    EXPECT_TRUE(RunUntil(kMaxJoinTicks, [&] { return InGame(client); })) << "a client couldn't join";
    return client;
  }

  void Rejoin(Z13TestWorld& client) {
    client.World().entity().set<JoinRequest>({.endpoint = {.host = "127.0.0.1", .port = z13::kDefaultServerPort}});
    EXPECT_TRUE(RunUntil(kMaxJoinTicks, [&] { return InGame(client); })) << "a client couldn't rejoin";
  }

  // Exit to the main menu: the client says goodbye and the server removes its player.
  void Leave(Z13TestWorld& client) {
    const uint32_t id = PlayerId(client);
    client.ExitToMainMenu();
    EXPECT_TRUE(RunUntil(kMaxJoinTicks, [&] { return !HasPlayer(*server_, id); })) << "player " << id << " stayed";
  }

  // The process is gone: only the transport's disconnect reaches the server.
  void Drop(Z13TestWorld& client) {
    const uint32_t id = PlayerId(client);
    std::erase_if(clients_, [&client](const auto& owned) { return owned.get() == &client; });
    EXPECT_TRUE(RunUntil(kMaxJoinTicks, [&] { return !HasPlayer(*server_, id); })) << "player " << id << " stayed";
  }

  void RunTicks(uint64_t ticks) {
    RunUntil(ticks, [] { return false; });
  }

  bool RunUntil(uint64_t max_ticks, const std::function<bool()>& condition) {
    std::vector<std::reference_wrapper<Z13TestWorld>> worlds {*server_};
    for (const auto& client : clients_) {
      worlds.emplace_back(*client);
    }
    return z13::testing::RunNetworkUntil(*network_, worlds, kNetTestDeltaTime, max_ticks, condition);
  }

  // Waits for fresh digests on every client in the game, then compares each world with the server's.
  void ExpectConverged() {
    std::map<Z13TestWorld*, uint64_t> targets;
    for (const auto& client : clients_) {
      if (InGame(*client)) {
        targets[client.get()] = client->World().get<StateDigests>().checked + kDigestChecks;
      }
    }
    ASSERT_TRUE(RunUntil(kMaxSettleTicks, [&] {
      return std::ranges::all_of(targets, [](const std::pair<Z13TestWorld* const, uint64_t>& target) {
        return target.first->World().get<StateDigests>().checked >= target.second;
      });
    })) << "digests stopped arriving";

    const std::set<uint32_t> players = PlayerIds(*server_);
    ExpectOneToolPerPlayer(*server_, players);
    for (const auto& client : clients_) {
      if (!InGame(*client)) {
        continue;
      }
      const uint32_t id = PlayerId(*client);
      EXPECT_EQ(client->World().get<StateDigests>().resyncs, 0u) << "player " << id << " was resynced";
      EXPECT_EQ(StateEntityNames(*client), StateEntityNames(*server_)) << "player " << id << " sees other entities";
      EXPECT_EQ(PlayerIds(*client), players);
      for (const uint32_t other : players) {
        EXPECT_LT((Position(*client, other) - Position(*server_, other)).norm(), z13::testing::kTestEpsilon)
            << "player " << id << " sees player " << other << " elsewhere";
      }
    }
  }

  static bool InGame(Z13TestWorld& client) {
    return client.World().has<z13::gameplay::Gameplay>() &&
           client.World().get<ConnectionStatus>().state == ConnectionState::kConnected;
  }

  static uint32_t PlayerId(Z13TestWorld& client) {
    return client.World().get<z13::gameplay::LocalPlayer>().id.value();
  }

  static bool HasPlayer(Z13TestWorld& world, uint32_t id) {
    return world.World().lookup(z13::gameplay::PlayerEntityName(id).c_str());
  }

  static std::set<uint32_t> PlayerIds(Z13TestWorld& world) {
    std::set<uint32_t> ids;
    world.World().each([&ids](const z13::gameplay::Player& player) { ids.insert(player.id); });
    return ids;
  }

  static Eigen::Vector3f Position(Z13TestWorld& world, uint32_t id) {
    const flecs::entity player = world.World().lookup(z13::gameplay::PlayerEntityName(id).c_str());
    return player ? z13::math::ExtractTranslation<float>(player.get<Eigen::Matrix4f>()) : Eigen::Vector3f::Zero();
  }

  static std::vector<std::string> StateEntityNames(Z13TestWorld& world) {
    std::vector<std::string> names;
    world.World().query_builder().with<z13::flecs_tools::StateEntity>().build().each(
        [&names](flecs::entity e) { names.emplace_back(e.path().c_str()); });
    std::ranges::sort(names);
    return names;
  }

  // A player who left takes nothing along and leaves nothing behind.
  static void ExpectOneToolPerPlayer(Z13TestWorld& world, const std::set<uint32_t>& players) {
    std::map<uint32_t, int> carried;
    world.World().each([&carried](const z13::inventory::Item&, const z13::inventory::CarriedBy& by) {
      ++carried[by.player_id];
    });
    std::map<uint32_t, int> expected;
    for (const uint32_t id : players) {
      expected[id] = 1;
    }
    EXPECT_EQ(carried, expected);
  }

  std::shared_ptr<InMemoryNetwork> network_ = std::make_shared<InMemoryNetwork>();
  std::unique_ptr<Z13TestWorld> server_ = std::make_unique<Z13TestWorld>(
      std::vector {std::string(kServerArg), std::string(kStationSceneArg), std::string(GetParam())}, network_);
  std::vector<std::unique_ptr<Z13TestWorld>> clients_;
};

TEST_P(SessionChurnTest, PlayersLeaveAndRejoinWhileOthersWalk) {
  Z13TestWorld& first = Join();
  Z13TestWorld& second = Join();
  const uint32_t first_id = PlayerId(first);

  second.EmitInput(KeyDown(Keycode::KEY_W));
  first.EmitInput(KeyDown(Keycode::KEY_D));
  RunTicks(kWalkTicks);
  Leave(first);  // with D still held
  RunTicks(kWalkTicks);
  Rejoin(first);
  EXPECT_NE(PlayerId(first), first_id) << "ids are never reused";
  first.EmitInput(KeyUp(Keycode::KEY_D));
  first.EmitInput(KeyDown(Keycode::KEY_S));
  Join();
  RunTicks(kWalkTicks);
  second.EmitInput(KeyUp(Keycode::KEY_W));
  first.EmitInput(KeyUp(Keycode::KEY_S));
  ExpectConverged();
}

TEST_P(SessionChurnTest, ADroppedClientIsRemovedAndAnotherTakesItsPlace) {
  Z13TestWorld& walker = Join();
  Z13TestWorld& dropped = Join();

  walker.EmitInput(KeyDown(Keycode::KEY_W));
  dropped.EmitInput(KeyDown(Keycode::KEY_A));
  RunTicks(kWalkTicks);
  Drop(dropped);
  Z13TestWorld& newcomer = Join();
  newcomer.EmitInput(KeyDown(Keycode::KEY_W));
  RunTicks(kWalkTicks);
  walker.EmitInput(KeyUp(Keycode::KEY_W));
  newcomer.EmitInput(KeyUp(Keycode::KEY_W));
  ExpectConverged();
}

// One player leaves while another is still joining: the PlayerLeft and PlayerJoined deltas cross.
TEST_P(SessionChurnTest, ALeaveDuringAnotherJoinConverges) {
  Z13TestWorld& walker = Join();
  Z13TestWorld& leaver = Join();

  walker.EmitInput(KeyDown(Keycode::KEY_W));
  clients_.push_back(std::make_unique<Z13TestWorld>(
      std::vector {std::string(kConnectArg), std::string(kTestServerEndpoint)}, network_));
  Z13TestWorld& joiner = *clients_.back();
  leaver.ExitToMainMenu();
  ASSERT_TRUE(RunUntil(kMaxJoinTicks, [&] { return InGame(joiner) && !InGame(leaver); }));
  RunTicks(kWalkTicks);
  walker.EmitInput(KeyUp(Keycode::KEY_W));
  ExpectConverged();
}

INSTANTIATE_TEST_SUITE_P(Site, SessionChurnTest, ::testing::Values(z13::testing::kSiteScene));
// Tens of seconds per join in Debug: --gtest_also_run_disabled_tests.
INSTANTIATE_TEST_SUITE_P(DISABLED_Spire, SessionChurnTest, ::testing::Values(z13::testing::kSpireScene));

}  // namespace
}  // namespace z13::net
