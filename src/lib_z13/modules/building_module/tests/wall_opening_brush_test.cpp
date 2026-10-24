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
#include <optional>
#include <vector>

#include <Eigen/Dense>

#include <z13/components/block.h>
#include <z13/components/block_brush.h>
#include <z13/components/building.h>
#include <z13/components/gameplay.h>
#include <z13/components/wall.h>

#include "../../z13_module/tests/support/building_test_helpers.h"
#include "../../z13_module/tests/support/z13_test_world.h"
#include "../src/wall_entities.h"
#include "support/station_builders.h"

// The Opening mode: a click cuts a door or a window in the wall aimed at.
namespace z13::building {
namespace {

using z13::station::BrushPreview;
using z13::station::OpeningKind;
using z13::station::WallAxis;
using z13::station::WallNode;
using z13::station::WallOpening;
using z13::station::WallPanelOf;
using z13::testing::AddBuilder;
using z13::testing::Facing;
using z13::testing::StationWorld;
using z13::testing::Z13TestWorld;

constexpr uint32_t kBuilderId = 7;
// The builder looks along +X at the middle of the wall, 5 cells up.
const Eigen::Vector3f kBuilderPosition {0.f, 0.1f, 1.25f};
constexpr int kStickLength = 8;
constexpr int kThickness = 2;
constexpr int kWallRun = 8;
const Eigen::Vector3i kFrom {8, -4, 0};
const Eigen::Vector3i kTo {8, 4, 0};
constexpr int kAimedStep = 4;
constexpr int kDefaultWidth = 4;

void AddWall(Z13TestWorld& test_world) {
  flecs::world world = test_world.World();
  auto& counters = world.get_mut<z13::gameplay::IdCounters>();
  const uint32_t a = SpawnWallNode(world, counters, kFrom).get<WallNode>().id;
  const uint32_t b = SpawnWallNode(world, counters, kTo).get<WallNode>().id;
  SpawnWallSegment(world, counters,
                   {.node_a = a, .node_b = b, .stick_a = {WallAxis::kZ, kStickLength},
                    .stick_b = {WallAxis::kZ, kStickLength}, .thickness = kThickness});
}

flecs::entity OpeningBuilder(Z13TestWorld& test_world, OpeningKind kind = OpeningKind::kDoor) {
  const flecs::entity builder = AddBuilder(test_world, kBuilderId, Facing(kBuilderPosition, Eigen::Vector3f::UnitX()));
  BuildTool& tool = z13::testing::BuildToolOf(builder).get_mut<BuildTool>();
  tool.mode = ToolMode::kOpening;
  tool.opening_kind = kind;
  test_world.Tick();
  return builder;
}

std::vector<WallOpening> Openings(flecs::world world) {
  std::vector<WallOpening> openings;
  world.query<const WallOpening>().each([&openings](const WallOpening& opening) { openings.push_back(opening); });
  return openings;
}

size_t PanelCount(flecs::world world) {
  return static_cast<size_t>(world.query<const WallPanelOf>().count());
}

BrushPreview PreviewOf(flecs::entity builder) {
  std::optional<BrushPreview> preview;
  builder.children([&preview](flecs::entity child) {
    if (child.has<BrushPreview>()) {
      preview = child.get<BrushPreview>();
    }
  });
  return preview.value();
}

void Click(Z13TestWorld& test_world, flecs::entity builder) {
  builder.add<RequestBuildBlock>();
  test_world.Tick();
  test_world.Tick();
}

TEST(WallOpeningBrushTest, AClickCutsADoorCentredOnTheAimAndLeavesWallAbove) {
  Z13TestWorld test_world = StationWorld();
  AddWall(test_world);
  const flecs::entity builder = OpeningBuilder(test_world);

  Click(test_world, builder);

  const std::vector<WallOpening> openings = Openings(test_world.World());
  ASSERT_EQ(openings.size(), 1u);
  EXPECT_EQ(openings.front().kind, OpeningKind::kDoor);
  EXPECT_EQ(openings.front().width, kDefaultWidth);
  EXPECT_EQ(openings.front().offset, kAimedStep - kDefaultWidth / 2);
  EXPECT_EQ(openings.front().sill, 0);
  EXPECT_LT(openings.front().height, kStickLength);
  EXPECT_EQ(PanelCount(test_world.World()), 4u);
}

TEST(WallOpeningBrushTest, AWindowIsCutAtASillWithAPane) {
  Z13TestWorld test_world = StationWorld();
  AddWall(test_world);
  const flecs::entity builder = OpeningBuilder(test_world, OpeningKind::kWindow);

  Click(test_world, builder);

  const std::vector<WallOpening> openings = Openings(test_world.World());
  ASSERT_EQ(openings.size(), 1u);
  EXPECT_GT(openings.front().sill, 0);
  EXPECT_LT(openings.front().sill + openings.front().height, kStickLength);
  EXPECT_EQ(PanelCount(test_world.World()), 5u);
}

TEST(WallOpeningBrushTest, TheToolsWidthSetsTheOpeningsWidth) {
  Z13TestWorld test_world = StationWorld();
  AddWall(test_world);
  const flecs::entity builder = OpeningBuilder(test_world);

  z13::testing::BuildToolOf(builder).get_mut<BuildTool>().opening_width = 2;
  Click(test_world, builder);

  ASSERT_EQ(Openings(test_world.World()).size(), 1u);
  EXPECT_EQ(Openings(test_world.World()).front().width, 2);
}

TEST(WallOpeningBrushTest, ThePreviewOutlinesTheOpeningOnBothFaces) {
  Z13TestWorld test_world = StationWorld();
  AddWall(test_world);
  const flecs::entity builder = OpeningBuilder(test_world);
  test_world.Tick();

  const BrushPreview preview = PreviewOf(builder);

  EXPECT_TRUE(preview.valid);
  EXPECT_EQ(preview.lines.size(), 8u);
}

TEST(WallOpeningBrushTest, AimingAtNoWallShowsNothingToCut) {
  Z13TestWorld test_world = StationWorld();
  const flecs::entity builder = OpeningBuilder(test_world);
  test_world.Tick();

  const BrushPreview preview = PreviewOf(builder);

  EXPECT_FALSE(preview.valid);
  EXPECT_TRUE(preview.lines.empty());
  Click(test_world, builder);
  EXPECT_TRUE(Openings(test_world.World()).empty());
}

TEST(WallOpeningBrushTest, AnOpeningWhereAnotherIsIsRefused) {
  Z13TestWorld test_world = StationWorld();
  AddWall(test_world);
  const flecs::entity builder = OpeningBuilder(test_world);
  Click(test_world, builder);
  test_world.Tick();

  EXPECT_FALSE(PreviewOf(builder).valid);
  Click(test_world, builder);

  EXPECT_EQ(Openings(test_world.World()).size(), 1u);
}

TEST(WallOpeningBrushTest, DestroyingAtTheGapTakesTheOpeningAndLeavesTheWall) {
  Z13TestWorld test_world = StationWorld();
  AddWall(test_world);
  const flecs::entity builder = OpeningBuilder(test_world);
  Click(test_world, builder);
  ASSERT_EQ(PanelCount(test_world.World()), 4u);

  builder.add<RequestDestroyBlock>();
  test_world.Tick();
  test_world.Tick();

  EXPECT_TRUE(Openings(test_world.World()).empty());
  EXPECT_EQ(test_world.World().count<z13::station::WallSegment>(), 1);
  EXPECT_EQ(PanelCount(test_world.World()), 1u);
}

TEST(WallOpeningBrushTest, AWallTooShortForTheOpeningIsRefused) {
  Z13TestWorld test_world = StationWorld();
  AddWall(test_world);
  const flecs::entity builder = OpeningBuilder(test_world);
  z13::testing::BuildToolOf(builder).get_mut<BuildTool>().opening_width = kWallRun;

  Click(test_world, builder);

  EXPECT_TRUE(Openings(test_world.World()).empty());
}

}  // namespace
}  // namespace z13::building
