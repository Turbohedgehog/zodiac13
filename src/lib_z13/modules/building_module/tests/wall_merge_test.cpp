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

#include <expected>
#include <string>
#include <vector>

#include <Eigen/Dense>

#include <lib_core/utils/math.h>
#include <z13/components/block.h>
#include <z13/components/block_brush.h>
#include <z13/components/building.h>
#include <z13/components/gameplay.h>
#include <z13/components/wall.h>
#include <z13_tests/shipped_station.h>

#include "../../z13_module/tests/support/building_test_helpers.h"
#include "../../z13_module/tests/support/z13_test_world.h"
#include "../src/wall_entities.h"
#include "../src/wall_merge.h"
#include "support/station_builders.h"

// The Merge mode: two wall nodes becoming one, and the wall a node on its line divides.
namespace z13::building {
namespace {

using z13::station::BrushPreview;
using z13::station::WallAxis;
using z13::station::WallMergeDraft;
using z13::station::WallNode;
using z13::station::WallPanelOf;
using z13::station::WallSegment;
using z13::testing::AddBuilder;
using z13::testing::Facing;
using z13::testing::StationWorld;
using z13::testing::Z13TestWorld;

constexpr uint32_t kBuilderId = 7;
const Eigen::Vector3f kBuilderPosition {0.f, 0.1f, 1.25f};
// Thick enough to stay airtight when a node moves and a wall leans.
constexpr int kThickness = 2;
constexpr int kJointReach = 16;
constexpr int kStickLength = 8;
const Eigen::Vector3i kOrigin {0, 0, 0};
const Eigen::Vector3i kFar {20, 0, 0};
const Eigen::Vector3i kNearOrigin {0, 1, 0};
const Eigen::Vector3i kElsewhere {30, 40, 0};

uint32_t AddNode(Z13TestWorld& test_world, const Eigen::Vector3i& cell) {
  flecs::world world = test_world.World();
  return SpawnWallNode(world, world.get_mut<z13::gameplay::IdCounters>(), cell).get<WallNode>().id;
}

void AddWall(Z13TestWorld& test_world, uint32_t a, uint32_t b) {
  flecs::world world = test_world.World();
  SpawnWallSegment(world, world.get_mut<z13::gameplay::IdCounters>(),
                   {.node_a = a, .node_b = b, .stick_a = {WallAxis::kZ, kStickLength},
                       .stick_b = {WallAxis::kZ, kStickLength}, .thickness = kThickness});
}

std::vector<WallSegment> Segments(flecs::world world) {
  std::vector<WallSegment> segments;
  world.query<const WallSegment>().each([&segments](const WallSegment& segment) { segments.push_back(segment); });
  return segments;
}

const z13::building::primitives::Palette& PaletteOf(Z13TestWorld& test_world) {
  return test_world.World().get<z13::building::primitives::BlockPalette>().palette;
}

std::expected<NodeMerge, std::string> Merge(Z13TestWorld& test_world, uint32_t first, uint32_t second, MergeAt at) {
  return PlanNodeMerge(test_world.World(), first, second, at, PaletteOf(test_world), kJointReach);
}

TEST(WallMergeTest, TwoNodesMeetInTheirMiddleRoundedDown) {
  Z13TestWorld test_world = StationWorld();
  const uint32_t a = AddNode(test_world, {0, 0, 0});
  const uint32_t b = AddNode(test_world, {8, 0, 0});
  const uint32_t c = AddNode(test_world, {13, 0, 0});
  const uint32_t d = AddNode(test_world, {20, 0, 0});
  AddWall(test_world, a, b);
  AddWall(test_world, c, d);

  const auto merge = Merge(test_world, b, c, MergeAt::kCenter);
  ASSERT_TRUE(merge.has_value());
  EXPECT_EQ(merge->cell, Eigen::Vector3i(10, 0, 0));
  EXPECT_EQ(Merge(test_world, b, c, MergeAt::kFirst)->cell,
            Eigen::Vector3i(8, 0, 0));
  EXPECT_EQ(Merge(test_world, b, c, MergeAt::kLast)->cell,
            Eigen::Vector3i(13, 0, 0));

  ApplyNodeMerge(test_world.World(), test_world.World().get_mut<z13::gameplay::IdCounters>(), *merge);
  test_world.Tick();
  test_world.Tick();

  EXPECT_EQ(WallNodeCell(test_world.World(), b), Eigen::Vector3i(10, 0, 0));
  EXPECT_FALSE(WallNodeCell(test_world.World(), c).has_value());
  const std::vector<WallSegment> segments = Segments(test_world.World());
  ASSERT_EQ(segments.size(), 2u);
  EXPECT_EQ(test_world.World().count<WallPanelOf>(), 2);
  for (const WallSegment& segment : segments) {
    EXPECT_TRUE(segment.node_a == b || segment.node_b == b);
  }
}

TEST(WallMergeTest, ANodeJoinedToItselfOrByAWallIsRefused) {
  Z13TestWorld test_world = StationWorld();
  const uint32_t a = AddNode(test_world, {0, 0, 0});
  const uint32_t b = AddNode(test_world, {8, 0, 0});
  AddWall(test_world, a, b);

  EXPECT_FALSE(Merge(test_world, a, a, MergeAt::kCenter).has_value());
  EXPECT_FALSE(Merge(test_world, a, b, MergeAt::kCenter).has_value());
}

TEST(WallMergeTest, AMergeThatLeavesAWallWithoutAPanelIsRefused) {
  Z13TestWorld test_world = StationWorld();
  const uint32_t p = AddNode(test_world, {0, 0, 0});
  const uint32_t q = AddNode(test_world, {8, 0, 0});
  const uint32_t r = AddNode(test_world, {8, 0, 0});
  AddWall(test_world, p, q);

  // Moving p onto r's place would put both ends of the wall on one point.
  const auto merge = Merge(test_world, p, r, MergeAt::kLast);
  EXPECT_FALSE(merge.has_value());
  EXPECT_TRUE(Merge(test_world, p, r, MergeAt::kFirst).has_value());
}

TEST(WallMergeTest, ANodeOnAWallDividesIt) {
  Z13TestWorld test_world = StationWorld();
  const uint32_t p = AddNode(test_world, {0, 0, 0});
  const uint32_t q = AddNode(test_world, {20, 0, 0});
  AddWall(test_world, p, q);
  const uint32_t x = AddNode(test_world, {8, 3, 0});
  const uint32_t x_far = AddNode(test_world, {8, 10, 0});
  const uint32_t y = AddNode(test_world, {12, -3, 0});
  const uint32_t y_far = AddNode(test_world, {12, -10, 0});
  AddWall(test_world, x, x_far);
  AddWall(test_world, y, y_far);

  const auto merge = Merge(test_world, x, y, MergeAt::kCenter);
  ASSERT_TRUE(merge.has_value());
  ASSERT_EQ(merge->cell, Eigen::Vector3i(10, 0, 0));
  ApplyNodeMerge(test_world.World(), test_world.World().get_mut<z13::gameplay::IdCounters>(), *merge);
  test_world.Tick();
  test_world.Tick();

  const std::vector<WallSegment> segments = Segments(test_world.World());
  ASSERT_EQ(segments.size(), 4u);
  int through_the_node = 0;
  for (const WallSegment& segment : segments) {
    through_the_node += (segment.node_a == x || segment.node_b == x) ? 1 : 0;
  }
  EXPECT_EQ(through_the_node, 4);
  EXPECT_EQ(test_world.World().count<WallPanelOf>(), 4);
}

TEST(WallMergeTest, TwoClicksMergeTheNodesUnderTheBrush) {
  Z13TestWorld test_world = StationWorld();
  const flecs::entity builder = AddBuilder(test_world, kBuilderId, Facing(kBuilderPosition, Eigen::Vector3f::UnitX()));
  z13::testing::BuildToolOf(builder).get_mut<BuildTool>().mode = ToolMode::kMerge;
  test_world.Tick();

  const auto brush_cell = [&] {
    std::optional<Eigen::Vector3i> cell;
    builder.children([&cell](flecs::entity child) {
      if (child.has<BrushPreview>()) {
        cell = child.get<BrushPreview>().block.cell;
      }
    });
    return cell.value();
  };
  const Eigen::Vector3i first_cell = brush_cell();
  const uint32_t a = AddNode(test_world, first_cell);
  const uint32_t a_far = AddNode(test_world, first_cell + Eigen::Vector3i(0, 10, 0));
  AddWall(test_world, a, a_far);
  builder.add<RequestBuildBlock>();
  test_world.Tick();
  ASSERT_TRUE(builder.has<WallMergeDraft>());
  EXPECT_EQ(builder.get<WallMergeDraft>().node_id, a);

  Eigen::Matrix4f transform = builder.get<Eigen::Matrix4f>();
  z13::math::SetTranslation(
      Eigen::Vector3f(z13::math::ExtractTranslation<float>(transform) + Eigen::Vector3f(0.f, 0.f, 2.f)), transform);
  builder.set(transform);
  test_world.Tick();
  const Eigen::Vector3i second_cell = brush_cell();
  const uint32_t b = AddNode(test_world, second_cell);
  const uint32_t b_far = AddNode(test_world, second_cell - Eigen::Vector3i(0, 10, 0));
  AddWall(test_world, b, b_far);
  builder.add<RequestBuildBlock>();
  test_world.Tick();

  EXPECT_FALSE(builder.has<WallMergeDraft>());
  EXPECT_FALSE(WallNodeCell(test_world.World(), b).has_value());
  EXPECT_TRUE(WallNodeCell(test_world.World(), a).has_value());
}

TEST(WallMergeTest, AMergedObliquePanelCanBeDestroyedAndTheOtherStays) {
  Z13TestWorld test_world = StationWorld();
  const flecs::entity builder = AddBuilder(test_world, kBuilderId, Facing(kBuilderPosition, Eigen::Vector3f::UnitX()));
  const uint32_t a = AddNode(test_world, {8, -4, 0});
  const uint32_t b = AddNode(test_world, {12, 4, 0});
  const uint32_t c = AddNode(test_world, {14, -4, 0});
  const uint32_t d = AddNode(test_world, {22, 4, 0});
  AddWall(test_world, a, b);
  AddWall(test_world, c, d);
  test_world.Tick();
  const auto merge = Merge(test_world, b, c, MergeAt::kCenter);
  ASSERT_TRUE(merge.has_value());
  ApplyNodeMerge(test_world.World(), test_world.World().get_mut<z13::gameplay::IdCounters>(), *merge);
  test_world.Tick();
  test_world.Tick();
  ASSERT_EQ(test_world.World().count<WallPanelOf>(), 2);

  builder.add<z13::building::RequestDestroyBlock>();
  test_world.Tick();
  test_world.Tick();
  test_world.Tick();

  EXPECT_EQ(test_world.World().count<WallSegment>(), 1);
  EXPECT_EQ(test_world.World().count<WallPanelOf>(), 1);
}

TEST(WallMergeTest, AMergeThatMakesAJointTooSharpIsRefused) {
  Z13TestWorld test_world = StationWorld();
  // Walls from b and c to a and d would leave the merged node about 3 degrees apart.
  const uint32_t a = AddNode(test_world, kOrigin);
  const uint32_t b = AddNode(test_world, kFar);
  const uint32_t c = AddNode(test_world, kElsewhere);
  const uint32_t d = AddNode(test_world, kNearOrigin);
  AddWall(test_world, a, b);
  AddWall(test_world, c, d);

  const auto merge = Merge(test_world, b, c, MergeAt::kFirst);

  ASSERT_FALSE(merge.has_value());
  EXPECT_NE(merge.error().find("joint"), std::string::npos) << merge.error();
}

}  // namespace
}  // namespace z13::building
