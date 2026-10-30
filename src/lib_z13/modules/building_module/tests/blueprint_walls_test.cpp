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

#include <cstdlib>
#include <format>
#include <fstream>
#include <string>
#include <vector>

#include <primitives/blueprint.h>
#include <primitives/geometry.h>
#include <primitives/placement.h>
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

TEST(BlueprintWallsTest, AnOpeningCutsItsWallIntoPieces) {
  const std::string json = R"({"blocks": [], "walls": [
      {"node_a": {"x": 0, "y": 0, "z": 0}, "node_b": {"x": 20, "y": 0, "z": 0}, "length_a": 12, "length_b": 12,
       "thickness": 2, "openings": [{"kind": "Door", "offset": 8, "width": 4, "height": 8}]},
      {"node_a": {"x": 0, "y": 10, "z": 0}, "node_b": {"x": 20, "y": 10, "z": 0}, "length_a": 12, "length_b": 12,
       "thickness": 2}]})";
  const auto walls = z13::building::primitives::ParseBlueprintWalls(json);
  const auto openings = z13::building::primitives::ParseBlueprintOpenings(json);
  ASSERT_TRUE(walls.has_value()) << walls.error();
  ASSERT_TRUE(openings.has_value()) << openings.error();
  ASSERT_EQ(openings->size(), walls->size());
  ASSERT_EQ((*openings)[0].size(), 1u);
  EXPECT_EQ((*openings)[0][0].kind, z13::station::OpeningKind::kDoor);
  EXPECT_EQ((*openings)[0][0].offset, 8);
  EXPECT_EQ((*openings)[0][0].width, 4);
  EXPECT_TRUE((*openings)[1].empty());

  const auto palette = z13::testing::ShippedPalette().value();
  const auto panels = BlueprintPanels(*walls, palette, BuildingTuning {}, *openings);
  ASSERT_TRUE(panels.has_value()) << panels.error();
  constexpr size_t kPiecesAroundADoor = 4;  // beside it twice, over it, and the door's own panel
  EXPECT_EQ(panels->size(), kPiecesAroundADoor + 1);
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


// Writes the wall panels of the shipped hub, seams and openings as the scene derives them, to $Z13_DUMP_PANELS for
// blender/model_scripts/hub_blueprint.py (draw_dump); skipped without it.
TEST(BlueprintWallsTest, DumpPanels) {
  const char* path = std::getenv("Z13_DUMP_PANELS");
  if (path == nullptr) {
    GTEST_SKIP();
  }
  const auto palette = z13::testing::ShippedPalette().value();
  const auto walls = z13::testing::ShippedBlueprintWalls("hub").value();
  const auto openings = z13::testing::ShippedBlueprintOpenings("hub").value();
  const auto panels = BlueprintPanels(walls, palette, BuildingTuning {}, openings).value();
  std::ofstream out(path);
  out << "[";
  bool first_solid = true;
  for (const z13::station::Block& panel : panels) {
    const auto pose = z13::building::primitives::PoseOf(panel);
    const auto solids = z13::building::primitives::BlockSolids(
        palette, z13::building::primitives::ShapeKeyOf(panel.spec));
    for (const auto& solid : solids) {
      out << (first_solid ? "" : ",") << "[";
      first_solid = false;
      for (size_t i = 0; i < solid.vertices.size(); ++i) {
        const Eigen::Vector3f v = pose.origin.cast<float>() + pose.rotation.cast<float>() * solid.vertices[i];
        out << (i ? "," : "") << std::format("[{},{},{}]", v.x(), v.y(), v.z());
      }
      out << "]";
    }
  }
  out << "]";
}

}  // namespace
}  // namespace z13::building
