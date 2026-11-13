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
#include <tuple>
#include <cstdint>
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <Eigen/Dense>

#include <primitives_generated.h>

#include <lib_core/state/world_serializer.h>
#include <lib_core/utils/math.h>

#include <net_module/in_memory_transport.h>

#include <z13/components/gameplay/gameplay.h>
#include <z13/components/station/block.h>
#include <z13/components/station/block_changes.h>
#include <z13/components/station/spawn_point.h>
#include <z13/components/station/station_mode.h>
#include <player/gameplay_entities.h>
#include <primitives/blueprint.h>
#include <primitives/palette.h>
#include <primitives/placement.h>
#include <primitives/station_assets.h>
#include <z13_tests/shipped_station.h>

#include "../../z13_module/tests/support/test_network.h"
#include "../../z13_module/tests/support/z13_test_world.h"
#include "../src/block_entities.h"
#include "../src/blueprint_walls.h"
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

Palette ShippedPalette() {
  return z13::testing::ShippedPalette().value();
}

std::vector<Block> ShippedStation(std::string_view scene, const Palette& palette) {
  return z13::testing::ShippedBlueprint(scene, palette).value();
}

std::vector<Block> Blocks(flecs::world world) {
  std::vector<Block> blocks;
  world.query_builder<const Block>().build().each([&blocks](const Block& block) { blocks.push_back(block); });
  std::ranges::sort(blocks, [](const Block& a, const Block& b) {
    return std::lexicographical_compare(a.cell.begin(), a.cell.end(), b.cell.begin(), b.cell.end());
  });
  return blocks;
}

// Entities may arrive in any order, and the two triangles of a bent wall share a cell and differ in their cuts: compare
// sorted by every field.
bool SameBlocks(std::vector<Block> a, std::vector<Block> b) {
  const auto key = [](const Block& block) {
    return std::tuple(block.grid, block.cell.x(), block.cell.y(), block.cell.z(), block.spec.type_id,
                      block.spec.size.x(), block.spec.size.y(), block.spec.size.z(), block.spec.slant,
                      block.spec.orientation, block.spec.corners, block.spec.face_cuts);
  };
  const auto before = [&key](const Block& x, const Block& y) { return key(x) < key(y); };
  std::ranges::sort(a, before);
  std::ranges::sort(b, before);
  return std::ranges::equal(
      a, b, [](const Block& x, const Block& y) { return x.grid == y.grid && x.spec == y.spec && x.cell == y.cell; });
}

std::vector<std::string> StationArgs(std::string_view scene) {
  return {std::string(z13::testing::kStationSceneArg), std::string(scene)};
}

// Every blueprint in the source assets: a new one is tested without touching the code.
class TestStationTest : public ::testing::TestWithParam<std::string> {
 protected:
  std::string_view Scene() const { return GetParam(); }
};

int64_t SpawnPoints(const std::vector<Block>& blocks, const Palette& palette) {
  return std::ranges::count_if(blocks, [&palette](const Block& block) {
    return palette.Find(block.spec.type_id)->get().Has(PrimitiveFlags::Spawn);
  });
}

TEST_P(TestStationTest, EveryBlockOfTheBlueprintIsAValidBuild) {
  const Palette palette = ShippedPalette();
  const std::vector<Block> blocks = ShippedStation(Scene(), palette);
  ASSERT_FALSE(blocks.empty());

  const Status valid = ValidateBlueprint(blocks, palette, BuildingTuning {});

  EXPECT_TRUE(valid.has_value()) << valid.error();
  EXPECT_GT(SpawnPoints(blocks, palette), 0) << "nowhere to spawn";
}

TEST_P(TestStationTest, OverlappingBlueprintIsNotPlaced) {
  const Palette palette = ShippedPalette();
  std::vector<Block> blocks = ShippedStation(Scene(), palette);
  blocks.push_back(blocks.front());

  const Status valid = ValidateBlueprint(blocks, palette, BuildingTuning {});

  ASSERT_FALSE(valid.has_value());
  EXPECT_NE(valid.error().find(std::format("block {}", blocks.size() - 1)), std::string::npos);
}

TEST(StationBlueprintTest, BlueprintNamingAnUnknownPrimitiveIsRejected) {
  const auto blocks = z13::building::primitives::ParseBlueprint(
      R"({"blocks": [{"primitive": "Teleporter", "cell": {"x": 0, "y": 0, "z": 0}, "size": {"x": 1, "y": 1, "z": 1}}]})",
      ShippedPalette());

  ASSERT_FALSE(blocks.has_value());
  EXPECT_NE(blocks.error().find("Teleporter"), std::string::npos);
}

TEST(StationBlueprintTest, ConduitRunsAreParsedWithTheirKindNameGridAndAxis) {
  const auto conduits = z13::building::primitives::ParseBlueprintConduits(
      R"({"grids": [{"id": 2}], "conduits": [
        {"grid": 0, "kind": "Cable", "cell": {"x": 1, "y": 2, "z": 3}, "axis": "Y", "length": 4},
        {"grid": 2, "kind": "Pipe", "cell": {"x": 5, "y": 6, "z": 7}, "axis": "X", "length": 1}]})");

  ASSERT_TRUE(conduits.has_value()) << conduits.error();
  ASSERT_EQ(conduits->size(), 2u);
  EXPECT_EQ((*conduits)[0].kind, "Cable");
  EXPECT_EQ((*conduits)[0].cell, Eigen::Vector3i(1, 2, 3));
  EXPECT_EQ((*conduits)[0].axis, z13::conduit::ConduitAxis::kY);
  EXPECT_EQ((*conduits)[0].length, 4);
  EXPECT_EQ((*conduits)[1].grid, 2u);
  EXPECT_EQ((*conduits)[1].axis, z13::conduit::ConduitAxis::kX);
}

TEST(StationBlueprintTest, ConduitRunOfAnUnknownGridOrWithoutLengthIsRejected) {
  EXPECT_FALSE(z13::building::primitives::ParseBlueprintConduits(
                   R"({"conduits": [{"grid": 9, "kind": "Cable", "length": 1}]})")
                   .has_value());
  EXPECT_FALSE(z13::building::primitives::ParseBlueprintConduits(
                   R"({"conduits": [{"grid": 0, "kind": "Cable", "length": 0}]})")
                   .has_value());
}

TEST(StationBlueprintTest, LiftsAreParsedWithTheirStopsAndButtonsAndOneWithoutATripIsRejected) {
  const auto lifts = z13::building::primitives::ParseBlueprintLifts(
      R"({"lifts": [{"primitive": "Floor", "size": {"x": 8, "y": 8, "z": 1}, "lower": {"x": 1, "y": 2, "z": 3},
        "upper": {"x": 1, "y": 2, "z": 15}, "lower_button": {"x": 4, "y": 0, "z": 0}, "upper_button": {"x": 5, "y": 0, "z": 0}, "trip_ticks": 90}]})");

  ASSERT_TRUE(lifts.has_value()) << lifts.error();
  ASSERT_EQ(lifts->size(), 1u);
  EXPECT_EQ(lifts->front().primitive, "Floor");
  EXPECT_EQ(lifts->front().upper, Eigen::Vector3i(1, 2, 15));
  EXPECT_EQ(lifts->front().trip_ticks, 90u);
  EXPECT_FALSE(z13::building::primitives::ParseBlueprintLifts(R"({"lifts": [{"primitive": "Floor"}]})").has_value());
}

TEST_P(TestStationTest, SceneBuildsTheStationAndSpawnsThePlayerOnAMarker) {
  Z13TestWorld test_world(StationArgs(Scene()));
  test_world.Tick();

  ASSERT_TRUE(test_world.World().has<StationMode>());
  const Palette palette = ShippedPalette();
  const std::vector<Block> shipped = ShippedStation(Scene(), palette);
  const auto walls = z13::testing::ShippedBlueprintWalls(Scene());
  ASSERT_TRUE(walls.has_value()) << walls.error();
  const auto openings = z13::testing::ShippedBlueprintOpenings(Scene());
  ASSERT_TRUE(openings.has_value()) << openings.error();
  const auto panels = BlueprintPanels(*walls, palette, BuildingTuning {}, *openings);
  ASSERT_TRUE(panels.has_value()) << panels.error();
  const auto grids = z13::testing::ShippedBlueprintGrids(Scene(), palette);
  ASSERT_TRUE(grids.has_value()) << grids.error();
  size_t expected = shipped.size() + panels->size();
  for (const auto& part : *grids) {
    const auto grid_panels = BlueprintPanels(part.walls, palette, BuildingTuning {}, part.openings);
    ASSERT_TRUE(grid_panels.has_value()) << grid_panels.error();
    expected += part.blocks.size() + grid_panels->size();
  }
  EXPECT_EQ(Blocks(test_world.World()).size(), expected);
  EXPECT_EQ(test_world.World().count<SpawnPoint>(), SpawnPoints(shipped, palette));
  const Eigen::Vector3f player = z13::math::ExtractTranslation<float>(
      test_world.World().lookup(PlayerEntityName(0).c_str()).get<Eigen::Matrix4f>());
  bool on_a_marker = false;
  test_world.World().query_builder<const SpawnPoint>().build().each([&](const SpawnPoint& point) {
    on_a_marker = on_a_marker || player.isApprox(z13::math::ExtractTranslation<float>(point.transform));
  });
  EXPECT_TRUE(on_a_marker);
}

TEST_P(TestStationTest, SaveAndLoadKeepEveryBlock) {
  Z13TestWorld station(StationArgs(Scene()));
  station.Tick();
  const auto snapshot = z13::flecs_tools::CaptureState(station.World());
  ASSERT_TRUE(snapshot.has_value()) << snapshot.error();
  Z13TestWorld loaded({std::string(z13::testing::kStationArg)});
  loaded.Tick();

  ASSERT_TRUE(z13::flecs_tools::RestoreWorld(loaded.World(), *snapshot).has_value());

  EXPECT_TRUE(SameBlocks(Blocks(loaded.World()), Blocks(station.World())));
}

TEST_P(TestStationTest, ClientJoiningGetsTheWholeStation) {
  const auto network = std::make_shared<z13::net::InMemoryNetwork>();
  Z13TestWorld server(z13::testing::WithServerArg(StationArgs(Scene())), network);
  Z13TestWorld client({std::string(kConnectArg), std::string(kTestServerEndpoint)}, network);

  ASSERT_TRUE(RunNetworkUntil(*network, {server, client}, kNetTestDeltaTime, kMaxNetTestTicks, [&client] {
    return client.World().has<Gameplay>() && client.World().lookup(PlayerEntityName(1).c_str());
  }));

  EXPECT_EQ(Blocks(client.World()).size(), Blocks(server.World()).size());
  EXPECT_TRUE(SameBlocks(Blocks(client.World()), Blocks(server.World())));
  EXPECT_EQ(client.World().count<SpawnPoint>(), server.World().count<SpawnPoint>());
}

TEST(StationBlueprintTest, ASlantedPanelSurvivesSaveAndLoad) {
  const Palette palette = ShippedPalette();
  const Block panel {
      .spec = {.type_id = palette.Find("Panel")->get().id, .size = {6, 2, 5}, .slant = 3, .face_cuts = {0, 0, 0, 0, 0, 1, 1, 0}},
      .cell = {400, 400, 40}};
  Z13TestWorld station(StationArgs(z13::testing::kSiteScene));
  station.Tick();
  z13::station::SpawnBlock(station.World().entity("SlantedPanel"), panel);
  station.Tick();
  const auto snapshot = z13::flecs_tools::CaptureState(station.World());
  ASSERT_TRUE(snapshot.has_value()) << snapshot.error();
  Z13TestWorld loaded({std::string(z13::testing::kStationArg)});
  loaded.Tick();

  ASSERT_TRUE(z13::flecs_tools::RestoreWorld(loaded.World(), *snapshot).has_value());

  EXPECT_EQ(loaded.World().lookup("SlantedPanel").get<Block>().spec.slant, 3);
  EXPECT_EQ(loaded.World().lookup("SlantedPanel").get<Block>().spec.face_cuts, panel.spec.face_cuts);
  EXPECT_TRUE(SameBlocks(Blocks(loaded.World()), Blocks(station.World())));
}

INSTANTIATE_TEST_SUITE_P(
    Scenes, TestStationTest,
    ::testing::ValuesIn(z13::testing::ShippedScenes()),
    [](const ::testing::TestParamInfo<std::string>& info) { return info.param; });

}  // namespace
}  // namespace z13::building
