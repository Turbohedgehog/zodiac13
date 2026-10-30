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

#include <memory>
#include <string>
#include <vector>

#include <Eigen/Dense>

#include <input_config_generated.h>
#include <lib_core/state/state_markers.h>
#include <net_module/in_memory_transport.h>
#include <net_module/state_digest.h>
#include <primitives/placement.h>
#include <z13/components/conduit.h>
#include <z13/components/inventory.h>
#include <z13/components/net.h>
#include <z13/components/state_changes.h>
#include <z13_module/gameplay/gameplay_entities.h>
#include <z13_settings/settings.h>

#include "../../building_module/tests/support/station_builders.h"
#include "../../z13_module/tests/support/building_test_helpers.h"
#include "../../z13_module/tests/support/test_network.h"
#include "../../z13_module/tests/support/z13_test_world.h"
#include "../src/conduit_tool_system.h"

// The conduit tool through the real input pipeline, and a conduit reaching a client that doesn't simulate its layer.
namespace z13::conduit {
namespace {

using z13::testing::Click;
using z13::testing::Tap;
using z13::testing::Z13TestWorld;
using Keycode = z13::fbs::input::Keycode;

std::vector<Conduit> Conduits(flecs::world world) {
  std::vector<Conduit> conduits;
  world.each([&conduits](const Conduit& conduit) { conduits.push_back(conduit); });
  return conduits;
}

// The local player with the conduit tool in the hand, facing a wall.
class ConduitToolTest : public ::testing::Test {
 protected:
  void SetUp() override {
    world_.Tick();
    world_.Player().set(z13::inventory::Hotbar {.active_cell = kConduitToolCell});
    z13::testing::Ticks(world_, z13::testing::kToolSettleTicks);
    wall_ = z13::testing::AddWallAhead(world_, world_.Player()).block;
  }

  // Two clicks at the same cell: a run of one.
  void LayOne() {
    Click(world_, Keycode::MOUSE_BUTTON_LEFT);
    Click(world_, Keycode::MOUSE_BUTTON_LEFT);
  }

  bool InWall(const Eigen::Vector3i& cell) const {
    return z13::building::primitives::OccupiedCells(wall_).Contains(cell);
  }

  Z13TestWorld world_ = z13::testing::StationWorld();
  z13::station::Block wall_;
};

TEST_F(ConduitToolTest, EveryPlayerIsGivenTheToolOnce) {
  int tools = 0;
  world_.World().each([&tools](const ConduitTool&) { ++tools; });
  z13::testing::Ticks(world_, 2);

  EXPECT_EQ(tools, 1);
  EXPECT_TRUE(world_.Player().has<ConduitToolGiven>());
}

TEST_F(ConduitToolTest, AConduitIsLaidIntoTheAimedWall) {
  LayOne();

  const std::vector<Conduit> conduits = Conduits(world_.World());
  ASSERT_EQ(conduits.size(), 1u);
  EXPECT_TRUE(InWall(conduits.front().cell));
}

TEST_F(ConduitToolTest, OnTheSurfaceItIsLaidInFrontOfTheWall) {
  Tap(world_, Keycode::KEY_V, z13::testing::TickOnly(world_));
  LayOne();

  const std::vector<Conduit> conduits = Conduits(world_.World());
  ASSERT_EQ(conduits.size(), 1u);
  EXPECT_FALSE(InWall(conduits.front().cell));
  EXPECT_TRUE(InWall(conduits.front().cell + Eigen::Vector3i::UnitX()));
}

TEST_F(ConduitToolTest, ACellTakesOneConduitOfANetworkAndOneOfAnother) {
  LayOne();
  LayOne();
  EXPECT_EQ(Conduits(world_.World()).size(), 1u);

  Tap(world_, Keycode::KEY_E, z13::testing::TickOnly(world_));
  LayOne();

  EXPECT_EQ(Conduits(world_.World()).size(), 2u);
}

TEST_F(ConduitToolTest, TheRemoveKeyTakesTheAimedConduitOut) {
  LayOne();
  ASSERT_EQ(Conduits(world_.World()).size(), 1u);

  Click(world_, Keycode::MOUSE_BUTTON_RIGHT);

  EXPECT_TRUE(Conduits(world_.World()).empty());
}

class ConduitNetTest : public ::testing::Test {
 protected:
  static constexpr uint64_t kMaxTicks = 1200;
  static constexpr uint64_t kSettleTicks = 90;
  // The clients 3 m apart behind the host; the observer simulates only the host (the lower id on a tie).
  static constexpr uint32_t kLayerId = 2;

  void SetUp() override {
    z13::Settings settings = z13::testing::TestSettings();
    settings.net->interest_events = true;
    settings.net->ghosts = true;
    settings.net->simulated_players_max = 1;
    settings.net->simulated_players_slack = 0;
    network_ = std::make_shared<z13::net::InMemoryNetwork>();
    server_ = std::make_unique<Z13TestWorld>(
        z13::testing::WithServerArg(z13::testing::SiteArgs()), network_, settings);
    for (int i = 0; i < 2; ++i) {
      clients_.push_back(std::make_unique<Z13TestWorld>(
          std::vector<std::string> {std::string(z13::testing::kConnectArg),
                                    std::string(z13::testing::kTestServerEndpoint)},
          network_, settings));
    }
    ASSERT_TRUE(z13::testing::RunNetworkUntil(*network_, Worlds(), z13::testing::kNetTestDeltaTime, kMaxTicks, [&] {
      return Connected(*clients_[0]) && Connected(*clients_[1]);
    }));
    Run();
  }

  static bool Connected(Z13TestWorld& client) {
    return client.World().get<z13::net::ConnectionStatus>().state == z13::net::ConnectionState::kConnected;
  }

  std::vector<std::reference_wrapper<Z13TestWorld>> Worlds() { return {*server_, *clients_[0], *clients_[1]}; }

  void Run() {
    z13::testing::RunNetworkUntil(
        *network_, Worlds(), z13::testing::kNetTestDeltaTime, kSettleTicks, [] { return false; });
  }

  std::shared_ptr<z13::net::InMemoryNetwork> network_;
  std::unique_ptr<Z13TestWorld> server_;
  std::vector<std::unique_ptr<Z13TestWorld>> clients_;
};

TEST_F(ConduitNetTest, AConduitLaidAndRemovedByAnotherPlayerReachesTheClient) {
  flecs::world observer = clients_[0]->World();
  ASSERT_FALSE(observer.lookup(z13::gameplay::PlayerEntityName(kLayerId).c_str()))
      << "the observer simulates the layer";
  flecs::world world = server_->World();
  const auto as_layer = [&](const auto& edit) {
    {
      const z13::gameplay::ActorScope scope(world.get_mut<z13::gameplay::EditActor>(), kLayerId);
      edit();
    }
    Run();
  };

  as_layer([&] {
    const flecs::entity conduit = world.entity("TestConduit").add<z13::flecs_tools::StateEntity>().set(
        Conduit {.kind = 1, .cell = {40, 0, 4}, .axis = ConduitAxis::kZ, .length = 8});
    z13::gameplay::RecordStateChange(conduit, z13::gameplay::StateChangeKind::kSet);
  });
  ASSERT_EQ(Conduits(observer).size(), 1u);
  EXPECT_EQ(Conduits(observer).front().length, 8);

  as_layer([&] {
    const flecs::entity conduit = world.lookup("TestConduit");
    z13::gameplay::RecordStateChange(conduit, z13::gameplay::StateChangeKind::kRemoved);
    conduit.destruct();
  });
  EXPECT_TRUE(Conduits(observer).empty());
  EXPECT_EQ(observer.get<z13::net::StateDigests>().resyncs, 0u);
}

}  // namespace
}  // namespace z13::conduit
