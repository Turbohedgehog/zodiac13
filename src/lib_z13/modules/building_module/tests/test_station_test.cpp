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
#include <format>
#include <memory>
#include <string>
#include <vector>

#include <Eigen/Dense>

#include <lib_core/state/world_serializer.h>
#include <lib_core/utils/math.h>

#include <net_module/in_memory_transport.h>

#include <z13/components/gameplay.h>
#include <z13/components/station.h>
#include <z13_module/gameplay/gameplay_entities.h>
#include <z13_primitives/blueprint.h>
#include <z13_primitives/palette.h>
#include <z13_primitives/placement.h>
#include <z13_tests/shipped_station.h>

#include "../../z13_module/tests/support/test_network.h"
#include "../../z13_module/tests/support/z13_test_world.h"
#include "../src/block_entities.h"
#include "../src/build_validation.h"
#include "support/station_builders.h"

namespace z13::building {
namespace {

using z13::building::primitives::Palette;
using z13::building::primitives::PrimitiveFlags;
using z13::gameplay::Gameplay;
using z13::gameplay::PlayerEntityName;
using z13::station::Block;
using z13::station::SpawnPoint;
using z13::station::StationMode;
using z13::testing::kConnectArg;
using z13::testing::kMaxNetTestTicks;
using z13::testing::kNetTestDeltaTime;
using z13::testing::kTestServerEndpoint;
using z13::testing::RunNetworkUntil;
using z13::testing::Z13TestWorld;

// Four markers in each habitat deck's lounge.
constexpr int kSpawnPoints = 8;

Palette ShippedPalette() {
  return z13::testing::ShippedPalette().value();
}

std::vector<Block> ShippedStation(const Palette& palette) {
  return z13::testing::ShippedBlueprint(z13::testing::kTestScene, palette).value();
}

std::vector<Block> Blocks(flecs::world world) {
  std::vector<Block> blocks;
  world.query_builder<const Block>().build().each([&blocks](const Block& block) { blocks.push_back(block); });
  std::ranges::sort(blocks, [](const Block& a, const Block& b) {
    return std::lexicographical_compare(a.cell.begin(), a.cell.end(), b.cell.begin(), b.cell.end());
  });
  return blocks;
}

bool SameBlocks(const std::vector<Block>& a, const std::vector<Block>& b) {
  return std::ranges::equal(a, b, [](const Block& x, const Block& y) { return x.spec == y.spec && x.cell == y.cell; });
}

std::vector<std::string> TestStationArgs() {
  return {std::string(z13::testing::kStationSceneArg), std::string(z13::testing::kTestScene)};
}

TEST(TestStationTest, EveryBlockOfTheBlueprintIsAValidBuild) {
  const Palette palette = ShippedPalette();
  const std::vector<Block> blocks = ShippedStation(palette);
  ASSERT_FALSE(blocks.empty());

  const Status valid = ValidateBlueprint(blocks, palette, BuildingTuning {});

  EXPECT_TRUE(valid.has_value()) << valid.error();
  EXPECT_EQ(std::ranges::count_if(blocks, [&palette](const Block& block) {
              return palette.Find(block.spec.type_id)->get().Has(PrimitiveFlags::Spawn);
            }),
            kSpawnPoints);
}

TEST(TestStationTest, OverlappingBlueprintIsNotPlaced) {
  const Palette palette = ShippedPalette();
  std::vector<Block> blocks = ShippedStation(palette);
  blocks.push_back(blocks.front());

  const Status valid = ValidateBlueprint(blocks, palette, BuildingTuning {});

  ASSERT_FALSE(valid.has_value());
  EXPECT_NE(valid.error().find(std::format("block {}", blocks.size() - 1)), std::string::npos);
}

TEST(TestStationTest, BlueprintNamingAnUnknownPrimitiveIsRejected) {
  const auto blocks = z13::building::primitives::ParseBlueprint(
      R"({"blocks": [{"primitive": "Teleporter", "cell": {"x": 0, "y": 0, "z": 0}, "size": {"x": 1, "y": 1, "z": 1}}]})",
      ShippedPalette());

  ASSERT_FALSE(blocks.has_value());
  EXPECT_NE(blocks.error().find("Teleporter"), std::string::npos);
}

TEST(TestStationTest, SceneBuildsTheStationAndSpawnsThePlayerOnAMarker) {
  Z13TestWorld test_world(TestStationArgs());
  test_world.Tick();

  ASSERT_TRUE(test_world.World().has<StationMode>());
  EXPECT_EQ(Blocks(test_world.World()).size(), ShippedStation(ShippedPalette()).size());
  EXPECT_EQ(test_world.World().count<SpawnPoint>(), kSpawnPoints);
  const Eigen::Vector3f player = z13::math::ExtractTranslation<float>(
      test_world.World().lookup(PlayerEntityName(0).c_str()).get<Eigen::Matrix4f>());
  bool on_a_marker = false;
  test_world.World().query_builder<const SpawnPoint>().build().each([&](const SpawnPoint& point) {
    on_a_marker = on_a_marker || player.isApprox(z13::math::ExtractTranslation<float>(point.transform));
  });
  EXPECT_TRUE(on_a_marker);
}

TEST(TestStationTest, SaveAndLoadKeepEveryBlock) {
  Z13TestWorld station(TestStationArgs());
  station.Tick();
  const auto snapshot = z13::flecs_tools::CaptureState(station.World());
  ASSERT_TRUE(snapshot.has_value()) << snapshot.error();
  Z13TestWorld loaded({std::string(z13::testing::kStationArg)});
  loaded.Tick();

  ASSERT_TRUE(z13::flecs_tools::RestoreWorld(loaded.World(), *snapshot).has_value());

  EXPECT_TRUE(SameBlocks(Blocks(loaded.World()), Blocks(station.World())));
}

TEST(TestStationTest, ClientJoiningGetsTheWholeStation) {
  const auto network = std::make_shared<z13::net::InMemoryNetwork>();
  Z13TestWorld server(z13::testing::WithServerArg(TestStationArgs()), network);
  Z13TestWorld client({std::string(kConnectArg), std::string(kTestServerEndpoint)}, network);

  ASSERT_TRUE(RunNetworkUntil(*network, {server, client}, kNetTestDeltaTime, kMaxNetTestTicks, [&client] {
    return client.World().has<Gameplay>() && client.World().lookup(PlayerEntityName(1).c_str());
  }));

  EXPECT_TRUE(SameBlocks(Blocks(client.World()), Blocks(server.World())));
  EXPECT_EQ(client.World().count<SpawnPoint>(), kSpawnPoints);
}

}  // namespace
}  // namespace z13::building
