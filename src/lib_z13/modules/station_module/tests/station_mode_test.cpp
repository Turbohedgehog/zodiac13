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

#include <Eigen/Dense>

#include <lib_core/state/world_serializer.h>
#include <lib_core/utils/math.h>

#include <net_module/in_memory_transport.h>

#include <z13/components/building.h>
#include <z13/components/gameplay.h>
#include <z13/components/net.h>
#include <z13/components/station.h>
#include <z13_module/gameplay/gameplay_entities.h>

#include "../../z13_module/tests/support/test_network.h"
#include "../../z13_module/tests/support/z13_test_world.h"

namespace z13::station {
namespace {

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

// The construction site's slab is 7×7 placeholder blocks.
constexpr int kSlabBlocks = 49;

int SpawnPoints(Z13TestWorld& world) {
  return world.World().count<SpawnPoint>();
}

int Blocks(Z13TestWorld& world) {
  return world.World().count<z13::building::BasicBlock>();
}

Eigen::Vector3f SpawnPointPosition(Z13TestWorld& world) {
  Eigen::Vector3f position = Eigen::Vector3f::Zero();
  world.World().query_builder<const Eigen::Matrix4f>().with<SpawnPoint>().build().each(
      [&position](const Eigen::Matrix4f& transform) { position = z13::math::ExtractTranslation<float>(transform); });
  return position;
}

Eigen::Vector3f PlayerPosition(Z13TestWorld& world, uint32_t id) {
  return z13::math::ExtractTranslation<float>(
      world.World().lookup(PlayerEntityName(id).c_str()).get<Eigen::Matrix4f>());
}

// Above the slab: every block's top face is below the spawn point.
bool IsAboveTheSlab(Z13TestWorld& world, const Eigen::Vector3f& position) {
  bool above = true;
  world.World().query_builder<const Eigen::Matrix4f>().with<z13::building::BasicBlock>().build().each(
      [&](const Eigen::Matrix4f& transform) {
        const float top = z13::math::ExtractTranslation<float>(transform).z() + z13::building::kBlockSize / 2.f;
        above = above && top < position.z();
      });
  return above;
}

TEST(StationModeTest, StationFlagStartsTheConstructionSite) {
  Z13TestWorld test_world(/*skip_main_menu=*/false, {std::string(kStationArg)});
  test_world.Tick();

  EXPECT_TRUE(test_world.World().has<StationMode>());
  EXPECT_TRUE(test_world.World().has<Gameplay>());
  EXPECT_EQ(Blocks(test_world), kSlabBlocks);
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
}

TEST(StationModeTest, ModeAddedBeforeStartGameBuildsTheSite) {
  Z13TestWorld test_world(/*skip_main_menu=*/false);
  test_world.World().add<StationMode>();
  test_world.StartGame();
  test_world.Tick();

  EXPECT_EQ(Blocks(test_world), kSlabBlocks);
  EXPECT_EQ(SpawnPoints(test_world), 1);
}

TEST(StationModeTest, ExitToMainMenuClearsTheSite) {
  Z13TestWorld test_world(/*skip_main_menu=*/false, {std::string(kStationArg)});
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
  Z13TestWorld station(/*skip_main_menu=*/false, {std::string(kStationArg)});
  station.Tick();

  ASSERT_TRUE(z13::flecs_tools::RestoreWorld(station.World(), *snapshot).has_value());

  EXPECT_FALSE(station.World().has<StationMode>());
  EXPECT_EQ(SpawnPoints(station), 0);
}

TEST(StationModeTest, ClientJoiningAStationServerGetsTheModeAndTheSite) {
  const auto network = std::make_shared<z13::net::InMemoryNetwork>();
  Z13TestWorld server(/*skip_main_menu=*/false, {std::string(kServerArg), std::string(kStationArg)}, network);
  Z13TestWorld client(
      /*skip_main_menu=*/false, {std::string(kConnectArg), std::string(kTestServerEndpoint)}, network);

  ASSERT_TRUE(RunNetworkUntil(*network, {server, client}, kNetTestDeltaTime, kMaxNetTestTicks, [&client] {
    return client.World().has<Gameplay>() && client.World().lookup(PlayerEntityName(1).c_str());
  }));

  EXPECT_TRUE(client.World().has<StationMode>());
  EXPECT_EQ(Blocks(client), kSlabBlocks);
  EXPECT_EQ(SpawnPoints(client), 1);
  // The only spawn point is taken by the host, so the joiner shares it.
  EXPECT_TRUE(PlayerPosition(client, 1).isApprox(SpawnPointPosition(client)));
}

TEST(StationModeTest, ClientJoiningAnOrdinaryServerDropsItsStaleMode) {
  const auto network = std::make_shared<z13::net::InMemoryNetwork>();
  Z13TestWorld server(/*skip_main_menu=*/false, {std::string(kServerArg)}, network);
  Z13TestWorld client(
      /*skip_main_menu=*/false, {std::string(kConnectArg), std::string(kTestServerEndpoint)}, network);
  client.World().add<StationMode>();

  ASSERT_TRUE(RunNetworkUntil(*network, {server, client}, kNetTestDeltaTime, kMaxNetTestTicks, [&client] {
    return client.World().has<Gameplay>() && client.World().lookup(PlayerEntityName(1).c_str());
  }));

  EXPECT_FALSE(client.World().has<StationMode>());
}

}  // namespace
}  // namespace z13::station
