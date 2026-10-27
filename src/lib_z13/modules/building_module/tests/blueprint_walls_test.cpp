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

#include <string>
#include <vector>

#include <primitives/blueprint.h>
#include <primitives/wall_panel.h>
#include <z13_tests/shipped_station.h>
#include <z13_settings/building_tuning.h>

#include "../src/blueprint_walls.h"
#include "../src/build_validation.h"

namespace z13::building {
namespace {

using z13::building::primitives::WallGeometry;
using z13::station::WallAxis;

constexpr int kFloorStick = 1;
constexpr int kBandWidth = 4;

// Two horizontal bands one after another, meeting at (20, 0) at 45 degrees.
const std::vector<WallGeometry> kBands {
    {.node_a = {0, 0, 0}, .node_b = {20, 0, 0}, .stick_a = {WallAxis::kZ, kFloorStick},
     .stick_b = {WallAxis::kZ, kFloorStick}, .thickness = kBandWidth},
    {.node_a = {20, 0, 0}, .node_b = {40, 20, 0}, .stick_a = {WallAxis::kZ, kFloorStick},
     .stick_b = {WallAxis::kZ, kFloorStick}, .thickness = kBandWidth},
};

TEST(BlueprintWallsTest, WallsAreReadFromTheJson) {
  const std::string json = R"({"blocks": [], "walls": [
      {"node_a": {"x": 1, "y": 2, "z": 3}, "node_b": {"x": 11, "y": 2, "z": 3}, "length_a": 5, "length_b": 5,
       "thickness": 2}]})";
  const auto walls = z13::building::primitives::ParseBlueprintWalls(json);
  ASSERT_TRUE(walls.has_value()) << walls.error();
  ASSERT_EQ(walls->size(), 1u);
  EXPECT_EQ((*walls)[0].node_a, Eigen::Vector3i(1, 2, 3));
  EXPECT_EQ((*walls)[0].node_b, Eigen::Vector3i(11, 2, 3));
  EXPECT_EQ((*walls)[0].stick_a.axis, WallAxis::kZ);
  EXPECT_EQ((*walls)[0].stick_b.length, 5);
  EXPECT_EQ((*walls)[0].thickness, 2);
}

TEST(BlueprintWallsTest, ABlueprintWithoutWallsHasNone) {
  const auto walls = z13::building::primitives::ParseBlueprintWalls(R"({"blocks": []})");
  ASSERT_TRUE(walls.has_value()) << walls.error();
  EXPECT_TRUE(walls->empty());
}

TEST(BlueprintWallsTest, WallsMeetingAtANodeAreStitchedAndPlaceable) {
  const auto palette = z13::testing::ShippedPalette().value();
  const auto panels = BlueprintPanels(kBands, palette, BuildingTuning {});
  ASSERT_TRUE(panels.has_value()) << panels.error();
  ASSERT_EQ(panels->size(), kBands.size());

  const auto lone = z13::building::primitives::WallPanelBlock(kBands[0], palette);
  ASSERT_TRUE(lone.has_value()) << lone.error();
  EXPECT_NE((*panels)[0], *lone);

  EXPECT_TRUE(ValidateBlueprint({}, palette, BuildingTuning {}, *panels).has_value());
}

TEST(BlueprintWallsTest, APanelOnABlockIsRefused) {
  const auto palette = z13::testing::ShippedPalette().value();
  const auto panels = BlueprintPanels(kBands, palette, BuildingTuning {});
  ASSERT_TRUE(panels.has_value()) << panels.error();
  const z13::station::Block floor {
      .spec = {.type_id = palette.Find("Floor").value().get().id, .size = {10, 10, 1}}, .cell = {5, 0, 0}};
  const std::vector<z13::station::Block> blocks {floor};
  const auto valid = ValidateBlueprint(blocks, palette, BuildingTuning {}, *panels);
  ASSERT_FALSE(valid.has_value());
  EXPECT_NE(valid.error().find("wall panel"), std::string::npos) << valid.error();
}


// A refused joint squares the walls' ends and leaves wedge-shaped gaps between them.
TEST(BlueprintWallsTest, TheWallsOfEveryShippedBlueprintAreStitchedWithoutRefusal) {
  for (const std::string& scene : z13::testing::ShippedScenes()) {
    const auto walls = z13::testing::ShippedBlueprintWalls(scene);
    ASSERT_TRUE(walls.has_value()) << scene << ": " << walls.error();
    EXPECT_TRUE(BlueprintJointRefusals(*walls, BuildingTuning {}).empty()) << scene;
  }
}

}  // namespace
}  // namespace z13::building
