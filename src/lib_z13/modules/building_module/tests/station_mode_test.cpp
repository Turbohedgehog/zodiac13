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
#include <memory>
#include <string>
#include <vector>

#include <Eigen/Dense>

#include <lib_core/state/world_serializer.h>
#include <lib_core/utils/math.h>

#include <net_module/in_memory_transport.h>

#include <z13/components/building.h>
#include <z13/components/gameplay.h>
#include <z13/components/net.h>
#include <z13/components/station.h>
#include <z13_primitives/placement.h>
#include <z13_module/gameplay/gameplay_entities.h>

#include "../../z13_module/tests/support/test_network.h"
#include "../../z13_module/tests/support/z13_test_world.h"
#include "support/station_builders.h"

namespace z13::building {
namespace {

using z13::station::BlockBrush;
using z13::station::SpawnPoint;
using z13::station::StationMode;

using z13::gameplay::Gameplay;
using z13::gameplay::PlayerEntityName;
using z13::testing::kConnectArg;
using z13::testing::kMaxNetTestTicks;
using z13::testing::kNetTestDeltaTime;
using z13::testing::kServerArg;
using z13::testing::kStationArg;
using z13::testing::kTestServerEndpoint;
using z13::testing::RunNetworkUntil;
using z13::testing::Z13TestWorld;

// A floor and a spawn marker.
constexpr int kSiteBlocks = 2;

int SpawnPoints(Z13TestWorld& world) {return world.World().count<SpawnPoint>();
}

int Blocks(Z13TestWorld& world) {return world.World().count<z13::station::Block>();
}

Eigen::Vector3f SpawnPointPosition(Z13TestWorld& world) {Eigen::Vector3f position = Eigen::Vector3f::Zero();
  world.World().query_builder<const SpawnPoint>().build().each(
      [&position](const SpawnPoint& point) { position = z13::math::ExtractTranslation<float>(point.transform); });
  return position;
}

Eigen::Vector3f PlayerPosition(Z13TestWorld& world, uint32_t id) {return z13::math::ExtractTranslation<float>(
      world.World().lookup(PlayerEntityName(id).c_str()).get<Eigen::Matrix4f>());
}

// Above the site: every block's top face is below the spawn point.
bool IsAboveTheSlab(Z13TestWorld& world, const Eigen::Vector3f& position) {bool above = true;
  world.World().query_builder<const z13::station::Block>().build().each([&](const z13::station::Block& block) {
    const auto cells = z13::building::primitives::OccupiedCells(block);
    const float top = static_cast<float>(cells.End().z()) * z13::station::kCellSize;
    above = above && top < position.z();
  });
  return above;
}

TEST(StationModeTest, StationFlagAloneStartsAnEmptyStation) {
  Z13TestWorld test_world({std::string(kStationArg)});
  test_world.Tick();

  EXPECT_TRUE(test_world.World().has<StationMode>());
  EXPECT_TRUE(test_world.World().has<Gameplay>());
  EXPECT_EQ(Blocks(test_world), 0);
}

TEST(StationModeTest, SiteSceneStartsTheConstructionSite) {
  Z13TestWorld test_world(z13::testing::SiteArgs());
  test_world.Tick();

  EXPECT_TRUE(test_world.World().has<StationMode>());
  EXPECT_TRUE(test_world.World().has<Gameplay>());
  EXPECT_EQ(Blocks(test_world), kSiteBlocks);
  ASSERT_EQ(SpawnPoints(test_world), 1);
  const Eigen::Vector3f player = PlayerPosition(test_world, 0);
  EXPECT_TRUE(player.isApprox(SpawnPointPosition(test_world)));
  EXPECT_TRUE(IsAboveTheSlab(test_world, player));
}

TEST(StationModeTest, WithoutTheFlagTheSceneIsUnchanged) {
  Z13TestWorld test_world;
  test_world.Tick();

  EXPECT_FALSE(test_world.World().has<StationMode>());
  EXPECT_EQ(Blocks(test_world), 0);
  EXPECT_EQ(SpawnPoints(test_world), 0);
  // Station systems stay off: no player gets a station brush.
  EXPECT_EQ(test_world.World().count<BlockBrush>(), 0);
}

TEST(StationModeTest, ModeAddedBeforeStartGameBuildsTheSite) {
  Z13TestWorld test_world(std::vector<std::string> {});
  test_world.World().add<StationMode>();
  test_world.World().set(z13::station::StationSceneChoice {.scene = std::string(z13::testing::kSiteScene)});
  test_world.StartGame();
  test_world.Tick();

  EXPECT_EQ(Blocks(test_world), kSiteBlocks);
  EXPECT_EQ(SpawnPoints(test_world), 1);
}

TEST(StationModeTest, ExitToMainMenuClearsTheSite) {
  Z13TestWorld test_world(z13::testing::SiteArgs());
  test_world.Tick();

  test_world.ExitToMainMenu();
  test_world.Tick();

  EXPECT_EQ(Blocks(test_world), 0);
  EXPECT_EQ(SpawnPoints(test_world), 0);
}

TEST(StationModeTest, RestoringAnOrdinarySnapshotLeavesStationMode) {
  Z13TestWorld ordinary;
  ordinary.Tick();
  const auto snapshot = z13::flecs_tools::CaptureState(ordinary.World());
  ASSERT_TRUE(snapshot.has_value());
  Z13TestWorld station({std::string(kStationArg)});
  station.Tick();

  ASSERT_TRUE(z13::flecs_tools::RestoreWorld(station.World(), *snapshot).has_value());

  EXPECT_FALSE(station.World().has<StationMode>());
  EXPECT_EQ(SpawnPoints(station), 0);
}

TEST(StationModeTest, ClientJoiningAStationServerGetsTheModeAndTheSite) {
  const auto network = std::make_shared<z13::net::InMemoryNetwork>();
  Z13TestWorld server(z13::testing::WithServerArg(z13::testing::SiteArgs()), network);
  Z13TestWorld client({std::string(kConnectArg), std::string(kTestServerEndpoint)}, network);

  ASSERT_TRUE(RunNetworkUntil(*network, {server, client}, kNetTestDeltaTime, kMaxNetTestTicks, [&client] {
    return client.World().has<Gameplay>() && client.World().lookup(PlayerEntityName(1).c_str());
  }));

  EXPECT_TRUE(client.World().has<StationMode>());
  EXPECT_EQ(Blocks(client), kSiteBlocks);
  EXPECT_EQ(SpawnPoints(client), 1);
  // The only spawn point is taken by the host, so the joiner shares it.
  EXPECT_TRUE(PlayerPosition(client, 1).isApprox(SpawnPointPosition(client)));
}

TEST(StationModeTest, ClientJoiningAnOrdinaryServerDropsItsStaleMode) {
  const auto network = std::make_shared<z13::net::InMemoryNetwork>();
  Z13TestWorld server({std::string(kServerArg)}, network);
  Z13TestWorld client({std::string(kConnectArg), std::string(kTestServerEndpoint)}, network);
  client.World().add<StationMode>();

  ASSERT_TRUE(RunNetworkUntil(*network, {server, client}, kNetTestDeltaTime, kMaxNetTestTicks, [&client] {
    return client.World().has<Gameplay>() && client.World().lookup(PlayerEntityName(1).c_str());
  }));

  EXPECT_FALSE(client.World().has<StationMode>());
}

}  // namespace
}  // namespace z13::building
