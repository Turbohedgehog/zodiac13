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
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <Eigen/Dense>

#include <net_module/in_memory_transport.h>
#include <net_module/state_digest.h>
#include <z13/components/block.h>
#include <z13/components/net.h>
#include <z13/components/state_changes.h>
#include <z13/components/wall.h>
#include <z13_module/gameplay/gameplay_entities.h>
#include <z13_settings/settings.h>

#include "../../z13_module/tests/support/test_network.h"
#include "../../z13_module/tests/support/z13_test_world.h"
#include "../src/wall_entities.h"
#include "support/station_builders.h"
#include "support/test_namer.h"

// A wall laid by a player a client doesn't simulate reaches the client as an edit, panels included.
namespace z13::building {
namespace {

using z13::station::WallAxis;
using z13::testing::kConnectArg;
using z13::testing::kNetTestDeltaTime;
using z13::testing::kTestServerEndpoint;
using z13::testing::RunNetworkUntil;
using z13::testing::Z13TestWorld;

constexpr uint64_t kMaxTicks = 1200;
constexpr uint64_t kSettleTicks = 90;
// The host stands first and the clients 3 m apart behind it; client 1 simulates the host (the lower id on a tie).
constexpr uint32_t kBuilderId = 2;
constexpr int kThickness = 2;
constexpr int kStickLength = 8;

std::map<std::string, Eigen::Vector3i> BlocksByName(flecs::world world) {
  std::map<std::string, Eigen::Vector3i> blocks;
  world.query_builder<const z13::station::Block>().build().each(
      [&](flecs::entity entity, const z13::station::Block& block) { blocks[entity.name().c_str()] = block.cell; });
  return blocks;
}

std::set<std::string> WallNames(flecs::world world) {
  std::set<std::string> names;
  const auto add = [&names](flecs::entity entity) { names.insert(entity.name().c_str()); };
  world.query<const z13::station::WallNode>().each([&](flecs::entity e, const z13::station::WallNode&) { add(e); });
  world.query<const z13::station::WallSegment>().each(
      [&](flecs::entity e, const z13::station::WallSegment&) { add(e); });
  return names;
}

class WallEditsNetTest : public ::testing::Test {
 protected:
  void SetUp() override {
    z13::Settings settings = z13::testing::TestSettings();
    settings.net->interest_events = true;
    settings.net->ghosts = true;
    settings.net->simulated_players_max = 1;
    settings.net->simulated_players_slack = 0;
    settings.net->ghost_radius_m = 5000.;
    settings.net->simulated_radius_m = 5000.;
    network_ = std::make_shared<z13::net::InMemoryNetwork>();
    server_ = std::make_unique<Z13TestWorld>(z13::testing::WithServerArg(z13::testing::SiteArgs()), network_, settings);
    for (int i = 0; i < 2; ++i) {
      clients_.push_back(std::make_unique<Z13TestWorld>(
          std::vector<std::string> {std::string(kConnectArg), std::string(kTestServerEndpoint)}, network_, settings));
    }
    ASSERT_TRUE(RunNetworkUntil(*network_, Worlds(), kNetTestDeltaTime, kMaxTicks, [&] {
      return Connected(*clients_[0]) && Connected(*clients_[1]);
    }));
    Run(kSettleTicks);
  }

  static bool Connected(Z13TestWorld& client) {
    return client.World().get<z13::net::ConnectionStatus>().state == z13::net::ConnectionState::kConnected;
  }

  std::vector<std::reference_wrapper<Z13TestWorld>> Worlds() {
    return {*server_, *clients_[0], *clients_[1]};
  }

  void Run(uint64_t ticks) {
    RunNetworkUntil(*network_, Worlds(), kNetTestDeltaTime, ticks, [] { return false; });
  }

  // Runs `edit` on the server as the builder's command, which the observer doesn't simulate.
  template <typename Edit>
  void AsBuilder(const Edit& edit) {
    flecs::world world = server_->World();
    {
      const z13::gameplay::ActorScope scope(world.get_mut<z13::gameplay::EditActor>(), kBuilderId);
      edit(world);
    }
    Run(kSettleTicks);
  }

  std::shared_ptr<z13::net::InMemoryNetwork> network_;
  std::unique_ptr<Z13TestWorld> server_;
  std::vector<std::unique_ptr<Z13TestWorld>> clients_;
};

TEST_F(WallEditsNetTest, AWallLaidAndRemovedByAnotherPlayerReachesTheClientWithItsPanels) {
  flecs::world observer = clients_[0]->World();
  ASSERT_FALSE(observer.lookup(z13::gameplay::PlayerEntityName(kBuilderId).c_str()))
      << "the observer simulates the builder";
  z13::station::WallId segment {};

  AsBuilder([&](flecs::world world) {
    segment = SpawnWall(world, z13::testing::TestNamer(world),
                        {.node_a = {0, 0, 0}, .node_b = {10, 0, 0}, .stick_a = {WallAxis::kZ, kStickLength},
                         .stick_b = {WallAxis::kZ, kStickLength}, .thickness = kThickness})
                  .get<z13::station::WallSegment>().id;
  });

  EXPECT_FALSE(WallNames(server_->World()).empty());
  EXPECT_EQ(WallNames(observer), WallNames(server_->World()));
  EXPECT_FALSE(BlocksByName(server_->World()).empty());
  EXPECT_EQ(BlocksByName(observer), BlocksByName(server_->World())) << "the panels differ";

  AsBuilder([&](flecs::world world) { DestroyWallSegment(world, segment); });

  EXPECT_TRUE(WallNames(observer).empty());
  EXPECT_EQ(BlocksByName(observer), BlocksByName(server_->World()));
  // Not the builder's own client: the edit was made on the server, not by a command of its own.
  EXPECT_EQ(observer.get<z13::net::StateDigests>().resyncs, 0u);
}

}  // namespace
}  // namespace z13::building
