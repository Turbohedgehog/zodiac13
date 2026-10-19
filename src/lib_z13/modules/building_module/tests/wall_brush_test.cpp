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

#include <optional>
#include <vector>

#include <Eigen/Dense>

#include <z13/components/block.h>
#include <z13/components/block_brush.h>
#include <z13/components/building.h>
#include <z13/components/gameplay.h>
#include <z13/components/player_parts.h>
#include <z13/components/wall.h>
#include <z13_tests/shipped_station.h>

#include "../../z13_module/tests/support/building_test_helpers.h"
#include "../../z13_module/tests/support/z13_test_world.h"
#include "../src/wall_brush.h"
#include "../src/wall_entities.h"
#include "support/station_builders.h"

// The Wall mode's three clicks: snapping, the draft, the side the wall thickens to.
namespace z13::building {
namespace {

using z13::station::Block;
using z13::station::BrushPreview;
using z13::station::WallAxis;
using z13::station::WallDraft;
using z13::station::WallPanelOf;
using z13::station::WallSegment;
using Keycode = z13::fbs::input::Keycode;
using z13::testing::AddBuilder;
using z13::testing::AddWallAhead;
using z13::testing::BlocksOfType;
using z13::testing::BuildToolOf;
using z13::testing::Click;
using z13::testing::kWallId;
using z13::testing::LocalBuilder;
using z13::testing::Facing;
using z13::testing::StationWorld;
using z13::testing::Z13TestWorld;

constexpr uint32_t kBuilderId = 7;
const Eigen::Vector3f kBuilderPosition {0.f, 0.1f, 1.25f};

Aim AimAtPoint(const Eigen::Vector3f& point) {
  return {.point = point};
}

TEST(WallBrushTest, AClickRoundsToTheGridOrSnapsToAnAnchor) {
  const std::vector<Eigen::Vector3i> anchors {{10, 0, 0}, {0, 0, 7}};
  EXPECT_EQ(WallClickPoint(AimAtPoint({3.4f, 2.6f, 1.f}), anchors, 1.5f), Eigen::Vector3i(3, 3, 1));
  EXPECT_EQ(WallClickPoint(AimAtPoint({9.2f, 0.4f, 0.f}), anchors, 1.5f), Eigen::Vector3i(10, 0, 0));
  EXPECT_EQ(WallClickPoint(AimAtPoint({9.2f, 0.4f, 0.f}), anchors, 0.f), Eigen::Vector3i(9, 0, 0));
}

TEST(WallBrushTest, AnchorsComeInOneOrderWhateverTheSpawnOrder) {
  Z13TestWorld test_world = StationWorld();
  flecs::world world = test_world.World();
  auto& counters = world.get_mut<z13::gameplay::IdCounters>();
  for (const Eigen::Vector3i& cell : {Eigen::Vector3i(5, 0, 0), Eigen::Vector3i(1, 9, 0), Eigen::Vector3i(1, 2, 3)}) {
    SpawnWallNode(world, counters, cell);
  }
  const std::vector<Eigen::Vector3i> expected {{1, 2, 3}, {1, 9, 0}, {5, 0, 0}};
  EXPECT_EQ(WallAnchors(world), expected);
}

Aim RayThrough(const Eigen::Vector3f& eye, const Eigen::Vector3f& target) {
  return {.eye = eye, .point = eye + (target - eye) * 0.5f};
}

const WallPicking kPicking {.snap_cells = 1.5f, .reach_cells = 64.f};

TEST(WallBrushTest, TheStickLiesInTheNearestPlaneTheViewRayMeets) {
  const Eigen::Vector3i first {0, 0, 0};
  // Meets the z plane first (t = 1), the y plane later (1.5); the x plane lies behind the eye.
  const auto in_z = PickStick(RayThrough({2.f, 3.f, 5.f}, {4.f, 1.f, 0.f}), first, {}, kPicking);
  ASSERT_TRUE(in_z.has_value());
  EXPECT_EQ(in_z->point, Eigen::Vector3i(4, 1, 0));
  EXPECT_EQ(in_z->axis, WallAxis::kZ);

  const auto in_y = PickStick(RayThrough({10.f, 4.f, 10.f}, {6.f, 0.f, 7.f}), first, {}, kPicking);
  ASSERT_TRUE(in_y.has_value());
  EXPECT_EQ(in_y->point, Eigen::Vector3i(6, 0, 7));
  EXPECT_EQ(in_y->axis, WallAxis::kY);
}

TEST(WallBrushTest, AnObliqueWallIsAsThickAsItNeedsToBeAirtight) {
  const auto palette = z13::testing::ShippedPalette().value();
  const z13::building::primitives::WallGeometry diagonal {
      .node_a = {0, 0, 0}, .node_b = {10, 10, 0}, .stick_axis = WallAxis::kZ, .stick_length = 3, .thickness = 1};
  const auto faced = FacingEye(diagonal, {5.f, -20.f, 1.f}, palette);
  ASSERT_TRUE(faced.has_value()) << faced.error();
  EXPECT_EQ(std::abs(faced->wall.thickness), 2);

  z13::building::primitives::WallGeometry straight = diagonal;
  straight.node_b = {10, 0, 0};
  const auto along = FacingEye(straight, {5.f, -20.f, 1.f}, palette);
  ASSERT_TRUE(along.has_value()) << along.error();
  EXPECT_EQ(std::abs(along->wall.thickness), 1);
}

TEST(WallBrushTest, TheFaceAimedAtChoosesThePlaneWhereTheyMeet) {
  const Eigen::Vector3i first {4, 4, 0};
  // From the player's eye the ray to a floor point beyond the first crosses the vertical planes
  // through it before the floor: the face it lands on decides.
  const Aim floor {.eye = {0.f, 0.f, 1.5f}, .point = {8.f, 7.f, 0.f}, .normal = {0, 0, 1}};
  const auto on_floor = PickStick(floor, first, {}, kPicking);
  ASSERT_TRUE(on_floor.has_value());
  EXPECT_EQ(on_floor->axis, WallAxis::kZ);
  EXPECT_EQ(on_floor->point, Eigen::Vector3i(8, 7, 0));

  const Aim wall {.eye = {0.f, 0.f, 1.5f}, .point = {4.f, 9.f, 3.f}, .normal = {1, 0, 0}};
  const auto on_wall = PickStick(wall, first, {}, kPicking);
  ASSERT_TRUE(on_wall.has_value());
  EXPECT_EQ(on_wall->axis, WallAxis::kX);
  EXPECT_EQ(on_wall->point, Eigen::Vector3i(4, 9, 3));

  // A face in another plane than any through the first point doesn't choose.
  const Aim elsewhere {.eye = {0.f, 0.f, 1.5f}, .point = {8.f, 7.f, 6.f}, .normal = {0, 0, 1}};
  const auto crossing = PickStick(elsewhere, first, {}, kPicking);
  ASSERT_TRUE(crossing.has_value());
  EXPECT_NE(crossing->axis, WallAxis::kZ);
}

TEST(WallBrushTest, NoStickWhenNoPlaneIsMetOrTheStickIsEmpty) {
  const Eigen::Vector3i first {0, 0, 0};
  EXPECT_FALSE(PickStick(RayThrough({1.f, 1.f, 1.f}, {2.f, 2.f, 2.f}), first, {}, kPicking).has_value());
  EXPECT_FALSE(PickStick(RayThrough({0.f, 0.f, 5.f}, {0.f, 0.f, 0.f}), first, {}, kPicking).has_value());
  const WallPicking short_reach {.snap_cells = 1.5f, .reach_cells = 0.5f};
  EXPECT_FALSE(PickStick(RayThrough({2.f, 3.f, 5.f}, {4.f, 1.f, 0.f}), first, {}, short_reach).has_value());
}

TEST(WallBrushTest, TheStickEndSnapsToAnAnchorInItsPlane) {
  const std::vector<Eigen::Vector3i> anchors {{4, 2, 0}, {4, 1, 3}};
  const auto pick = PickStick(RayThrough({2.f, 3.f, 5.f}, {4.f, 1.f, 0.f}), {0, 0, 0}, anchors, kPicking);
  ASSERT_TRUE(pick.has_value());
  EXPECT_EQ(pick->point, Eigen::Vector3i(4, 2, 0));
}

TEST(WallBrushTest, TheHeightIsWhereTheRayComesNearestTheAxisLineAlongTheWallsPlane) {
  const Eigen::Vector3i first {0, 0, 0};
  const Eigen::Vector3i second {10, 0, 0};
  EXPECT_EQ(PickHeight(RayThrough({10.f, 0.f, 4.f}, {5.f, 0.f, 4.f}), first, second, WallAxis::kZ, {}, kPicking), 4);
  EXPECT_EQ(PickHeight(RayThrough({10.f, 0.f, -3.f}, {5.f, 0.f, -3.f}), first, second, WallAxis::kZ, {}, kPicking), -3);
  // Along the axis there is no nearest point, and at the first point no height.
  EXPECT_FALSE(PickHeight(RayThrough({0.f, 0.f, 9.f}, {0.f, 0.f, 4.f}), first, second, WallAxis::kZ, {}, kPicking));
  EXPECT_FALSE(PickHeight(RayThrough({10.f, 0.f, 0.f}, {5.f, 0.f, 0.f}), first, second, WallAxis::kZ, {}, kPicking));
}

TEST(WallBrushTest, TheHeightIsWhereTheRayMeetsTheWallsPlane) {
  const Eigen::Vector3i first {0, 0, 0};
  const Eigen::Vector3i second {10, 0, 0};
  // Aimed at the air above the stick's middle, from beside its start: the nearest approach to the
  // line at the first point would read 3, the plane gives the 4 aimed at.
  EXPECT_EQ(PickHeight(RayThrough({-5.f, -10.f, 1.5f}, {5.f, 0.f, 4.f}), first, second, WallAxis::kZ, {}, kPicking), 4);
  // Beyond the stick's end the plane goes on.
  EXPECT_EQ(PickHeight(RayThrough({3.f, -10.f, 1.5f}, {16.f, 0.f, -2.f}), first, second, WallAxis::kZ, {}, kPicking),
            -2);
  // A ray that points away from the plane finds nothing.
  EXPECT_FALSE(PickHeight(RayThrough({3.f, 10.f, 1.5f}, {3.f, 20.f, 4.f}), first, second, WallAxis::kZ, {}, kPicking));
  EXPECT_EQ(PickHeight(RayThrough({-5.f, -10.f, 1.5f}, {5.f, 0.f, 4.f}), first, first + Eigen::Vector3i(0, 10, 0),
                       WallAxis::kZ, {}, kPicking),
            3);
}

TEST(WallBrushTest, ThreeClicksMakeTheWallAndItThickensTowardTheEye) {
  const auto palette = z13::testing::ShippedPalette().value();
  const std::vector<Eigen::Vector3i> none;
  const auto first = ClickWall(std::nullopt, AimAtPoint({0.2f, 0.f, 0.f}), none, kPicking, 2);
  ASSERT_TRUE(first && first->draft);
  EXPECT_EQ(first->draft->stage, WallDraft::Stage::kStick);

  const auto second = ClickWall(first->draft, RayThrough({6.f, 9.f, 5.f}, {12.f, 4.f, 0.f}), none, kPicking, 2);
  ASSERT_TRUE(second && second->draft);
  EXPECT_EQ(second->draft->stage, WallDraft::Stage::kHeight);
  EXPECT_EQ(second->draft->stick_axis, WallAxis::kZ);
  EXPECT_EQ(second->draft->second, Eigen::Vector3i(12, 4, 0));

  const auto third = ClickWall(second->draft, RayThrough({30.f, 0.f, 8.f}, {20.f, 0.f, 8.f}), none, kPicking, 2);
  ASSERT_TRUE(third && third->wall);
  EXPECT_EQ(third->wall->stick_length, 8);

  const auto near_low = FacingEye(*third->wall, {6.f, -20.f, 4.f}, palette);
  const auto near_high = FacingEye(*third->wall, {6.f, 30.f, 4.f}, palette);
  ASSERT_TRUE(near_low && near_high);
  EXPECT_LT(near_low->wall.thickness * near_high->wall.thickness, 0);
  EXPECT_NE(near_low->block.cell, near_high->block.cell);
}

TEST(WallBrushTest, TheStickPreviewDrawsTheStickAndTheArrowAlongTheLeftOverAxis) {
  const auto palette = z13::testing::ShippedPalette().value();
  const WallDraft draft {.stage = WallDraft::Stage::kStick, .first = {0, 0, 0}};
  const WallPreview preview =
      PreviewWall(draft, RayThrough({2.f, 3.f, 5.f}, {4.f, 1.f, 0.f}), {}, kPicking, 2, palette);
  EXPECT_TRUE(preview.makeable);
  EXPECT_EQ(preview.block.cell, Eigen::Vector3i(4, 1, 0));
  ASSERT_EQ(preview.lines.size(), 4u);
  EXPECT_EQ(preview.lines[0].to, Eigen::Vector3f(4.f, 1.f, 0.f));
  // The arrow leaves the middle of the stick along z, toward the eye above it.
  EXPECT_EQ(preview.lines[1].from, Eigen::Vector3f(2.f, 0.5f, 0.f));
  EXPECT_GT(preview.lines[1].to.z(), 0.f);
  EXPECT_EQ(preview.lines[1].to.head<2>(), preview.lines[1].from.head<2>());
}

TEST(WallBrushTest, TheClicksCarryTheDraftAndARayAlongTheAxisFindsNoLength) {
  Z13TestWorld test_world = StationWorld();
  const flecs::entity builder = AddBuilder(test_world, kBuilderId, Facing(kBuilderPosition, Eigen::Vector3f::UnitX()));
  z13::testing::BuildToolOf(builder).get_mut<BuildTool>().mode = ToolMode::kWall;
  test_world.Tick();
  std::optional<Eigen::Vector3i> first;
  builder.children([&first](flecs::entity child) {
    if (child.has<BrushPreview>()) {
      first = child.get<BrushPreview>().block.cell;
    }
  });
  ASSERT_TRUE(first.has_value());
  builder.add<RequestBuildBlock>();
  test_world.Tick();
  ASSERT_TRUE(builder.has<WallDraft>());
  EXPECT_EQ(builder.get<WallDraft>().first, *first);

  // The builder looks along x, so the only plane its ray meets is the one across x through the
  // first point; the stick ends where the ray crosses it.
  const Eigen::Vector3i eye_cells {0, 6, 9};
  builder.set(Facing(eye_cells.cast<float>() * z13::station::kCellSize, Eigen::Vector3f::UnitX()));
  test_world.Tick();
  builder.add<RequestBuildBlock>();
  test_world.Tick();
  const WallDraft draft = builder.get<WallDraft>();
  EXPECT_EQ(draft.stage, WallDraft::Stage::kHeight);
  EXPECT_EQ(draft.stick_axis, WallAxis::kX);
  EXPECT_EQ(draft.second, Eigen::Vector3i(first->x(), 6, 9));

  // The third click's ray runs along the axis it measures: nothing to measure, the draft stays.
  builder.add<RequestBuildBlock>();
  test_world.Tick();
  EXPECT_EQ(builder.get<WallDraft>().stage, WallDraft::Stage::kHeight);
  EXPECT_EQ(test_world.World().count<WallSegment>(), 0);
}

TEST(WallBrushTest, AWallGetsOneSegmentAndSharesTheNodesItMeets) {
  Z13TestWorld test_world = StationWorld();
  flecs::world world = test_world.World();
  auto& counters = world.get_mut<z13::gameplay::IdCounters>();
  const z13::building::primitives::WallGeometry first {
      .node_a = {0, 0, 0}, .node_b = {12, 4, 0}, .stick_axis = WallAxis::kZ, .stick_length = 8, .thickness = 2};
  auto second = first;
  second.node_a = first.node_b;
  second.node_b = {24, 4, 0};
  SpawnWall(world, counters, first);
  SpawnWall(world, counters, second);
  test_world.Tick();
  test_world.Tick();

  EXPECT_EQ(world.count<WallSegment>(), 2);
  EXPECT_EQ(world.count<z13::station::WallNode>(), 3);
  EXPECT_EQ(world.count<WallPanelOf>(), 2);
}

TEST(WallBrushTest, TheDestroyButtonDropsTheWallInTheMakingBeforeItDestroysAnything) {
  Z13TestWorld test_world = StationWorld();
  const flecs::entity player = LocalBuilder(test_world);
  AddWallAhead(test_world, player);
  const size_t walls = BlocksOfType(test_world.World(), kWallId).size();
  BuildToolOf(player).get_mut<BuildTool>().mode = ToolMode::kWall;
  test_world.Tick();

  Click(test_world, Keycode::MOUSE_BUTTON_LEFT);
  ASSERT_TRUE(player.has<WallDraft>());
  Click(test_world, Keycode::MOUSE_BUTTON_RIGHT);
  EXPECT_FALSE(player.has<WallDraft>());
  EXPECT_EQ(BlocksOfType(test_world.World(), kWallId).size(), walls);

  Click(test_world, Keycode::MOUSE_BUTTON_RIGHT);
  EXPECT_EQ(BlocksOfType(test_world.World(), kWallId).size(), walls - 1);
}

TEST(WallBrushTest, TheWheelSetsTheThicknessInTheWallMode) {
  Z13TestWorld test_world = StationWorld();
  const flecs::entity player = LocalBuilder(test_world);
  BuildToolOf(player).get_mut<BuildTool>().mode = ToolMode::kWall;
  test_world.Tick();
  Click(test_world, Keycode::MOUSE_WHEEL_UP);
  EXPECT_EQ(BuildToolOf(player).get<BuildTool>().wall_thickness, 2);
  Click(test_world, Keycode::MOUSE_WHEEL_DOWN);
  Click(test_world, Keycode::MOUSE_WHEEL_DOWN);
  EXPECT_EQ(BuildToolOf(player).get<BuildTool>().wall_thickness, 1);
}

}  // namespace
}  // namespace z13::building
