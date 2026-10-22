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
#include <primitives/wall_bend.h>
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
constexpr float kSnapCells = 1.5f;

Aim AimAtPoint(const Eigen::Vector3f& point) {
  return {.point = point};
}

TEST(WallBrushTest, AClickRoundsToTheGridOrSnapsToAnAnchor) {
  const std::vector<Eigen::Vector3i> anchors {{10, 0, 0}, {0, 0, 7}};
  EXPECT_EQ(WallClickPoint(AimAtPoint({3.4f, 2.6f, 1.f}), anchors, kSnapCells), Eigen::Vector3i(3, 3, 1));
  EXPECT_EQ(WallClickPoint(AimAtPoint({9.2f, 0.4f, 0.f}), anchors, kSnapCells), Eigen::Vector3i(10, 0, 0));
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

constexpr float kEyeHeight = 1.5f;
constexpr int kThickness = 2;
const WallPicking kPicking {.snap_cells = kSnapCells, .reach_cells = 64.f};
const Eigen::Vector3i kOrigin {0, 0, 0};
const Eigen::Vector3i kEast {10, 0, 0};
// A view from above and aside onto a floor point: meets the z plane through the origin first.
const Eigen::Vector3f kDownEye {2.f, 3.f, 5.f};
const Eigen::Vector3f kFloorTarget {4.f, 1.f, 0.f};

Aim DownRay() {
  return RayThrough(kDownEye, kFloorTarget);
}

std::optional<int> HeightAt(
    const Aim& aim, const Eigen::Vector3i& first, const Eigen::Vector3i& second) {
  return PickHeight(aim, first, second, WallAxis::kZ, {}, kPicking);
}

TEST(WallBrushTest, TheStickLiesInTheNearestPlaneTheViewRayMeets) {
  const Eigen::Vector3i first = kOrigin;
  // Meets the z plane first (t = 1), the y plane later (1.5); the x plane lies behind the eye.
  const auto in_z = PickStick(DownRay(), first, {}, kPicking);
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
      .node_a = {0, 0, 0}, .node_b = {10, 10, 0}, .stick_a = {WallAxis::kZ, 3}, .stick_b = {WallAxis::kZ, 3},
      .thickness = 1};
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
  const Aim floor {.eye = {0.f, 0.f, kEyeHeight}, .point = {8.f, 7.f, 0.f}, .normal = {0, 0, 1}};
  const auto on_floor = PickStick(floor, first, {}, kPicking);
  ASSERT_TRUE(on_floor.has_value());
  EXPECT_EQ(on_floor->axis, WallAxis::kZ);
  EXPECT_EQ(on_floor->point, Eigen::Vector3i(8, 7, 0));

  const Aim wall {.eye = {0.f, 0.f, kEyeHeight}, .point = {4.f, 9.f, 3.f}, .normal = {1, 0, 0}};
  const auto on_wall = PickStick(wall, first, {}, kPicking);
  ASSERT_TRUE(on_wall.has_value());
  EXPECT_EQ(on_wall->axis, WallAxis::kX);
  EXPECT_EQ(on_wall->point, Eigen::Vector3i(4, 9, 3));

  // A face in another plane than any through the first point doesn't choose.
  const Aim elsewhere {.eye = {0.f, 0.f, kEyeHeight}, .point = {8.f, 7.f, 6.f}, .normal = {0, 0, 1}};
  const auto crossing = PickStick(elsewhere, first, {}, kPicking);
  ASSERT_TRUE(crossing.has_value());
  EXPECT_NE(crossing->axis, WallAxis::kZ);
}

TEST(WallBrushTest, NoStickWhenNoPlaneIsMetOrTheStickIsEmpty) {
  const Eigen::Vector3i first = kOrigin;
  EXPECT_FALSE(PickStick(RayThrough({1.f, 1.f, 1.f}, {2.f, 2.f, 2.f}), first, {}, kPicking).has_value());
  EXPECT_FALSE(PickStick(RayThrough({0.f, 0.f, 5.f}, {0.f, 0.f, 0.f}), first, {}, kPicking).has_value());
  const WallPicking short_reach {.snap_cells = kSnapCells, .reach_cells = 0.5f};
  EXPECT_FALSE(PickStick(DownRay(), first, {}, short_reach).has_value());
}

TEST(WallBrushTest, TheStickEndSnapsToAnAnchorInItsPlane) {
  const std::vector<Eigen::Vector3i> anchors {{4, 2, 0}, {4, 1, 3}};
  const auto pick = PickStick(DownRay(), kOrigin, anchors, kPicking);
  ASSERT_TRUE(pick.has_value());
  EXPECT_EQ(pick->point, Eigen::Vector3i(4, 2, 0));
}

TEST(WallBrushTest, TheHeightIsWhereTheRayComesNearestTheAxisLineAlongTheWallsPlane) {
  const Eigen::Vector3i first = kOrigin;
  const Eigen::Vector3i second = kEast;
  EXPECT_EQ(HeightAt(RayThrough({10.f, 0.f, 4.f}, {5.f, 0.f, 4.f}), first, second), 4);
  EXPECT_EQ(HeightAt(RayThrough({10.f, 0.f, -3.f}, {5.f, 0.f, -3.f}), first, second), -3);
  // Along the axis there is no nearest point, and at the first point no height.
  EXPECT_FALSE(HeightAt(RayThrough({0.f, 0.f, 9.f}, {0.f, 0.f, 4.f}), first, second));
  EXPECT_FALSE(HeightAt(RayThrough({10.f, 0.f, 0.f}, {5.f, 0.f, 0.f}), first, second));
}

TEST(WallBrushTest, TheHeightIsWhereTheRayMeetsTheWallsPlane) {
  const Eigen::Vector3i first = kOrigin;
  const Eigen::Vector3i second = kEast;
  // Aimed at the air above the stick's middle, from beside its start: the nearest approach to the
  // line at the first point would read 3, the plane gives the 4 aimed at.
  EXPECT_EQ(HeightAt(RayThrough({-5.f, -10.f, kEyeHeight}, {5.f, 0.f, 4.f}), first, second), 4);
  // Beyond the stick's end the plane goes on.
  EXPECT_EQ(HeightAt(RayThrough({3.f, -10.f, kEyeHeight}, {16.f, 0.f, -2.f}), first, second),
            -2);
  // A ray that points away from the plane finds nothing.
  EXPECT_FALSE(HeightAt(RayThrough({3.f, 10.f, kEyeHeight}, {3.f, 20.f, 4.f}), first, second));
  EXPECT_EQ(HeightAt(RayThrough({-5.f, -10.f, kEyeHeight}, {5.f, 0.f, 4.f}), first, first + Eigen::Vector3i(0, 10, 0)),
            3);
}

TEST(WallBrushTest, ThreeClicksMakeTheWallAndItThickensTowardTheEye) {
  const auto palette = z13::testing::ShippedPalette().value();
  const std::vector<Eigen::Vector3i> none;
  const auto first = ClickWall(std::nullopt, AimAtPoint({0.2f, 0.f, 0.f}), none, kPicking, kThickness);
  ASSERT_TRUE(first && first->draft);
  EXPECT_EQ(first->draft->stage, WallDraft::Stage::kStick);

  const auto second = ClickWall(first->draft, RayThrough({6.f, 9.f, 5.f}, {12.f, 4.f, 0.f}), none, kPicking, kThickness);
  ASSERT_TRUE(second && second->draft);
  EXPECT_EQ(second->draft->stage, WallDraft::Stage::kHeight);
  EXPECT_EQ(second->draft->stick_axis, WallAxis::kZ);
  EXPECT_EQ(second->draft->second, Eigen::Vector3i(12, 4, 0));

  const auto third = ClickWall(second->draft, RayThrough({30.f, 0.f, 8.f}, {20.f, 0.f, 8.f}), none, kPicking, kThickness);
  ASSERT_TRUE(third && third->wall);
  EXPECT_EQ(third->wall->stick_a.length, 8);

  const auto near_low = FacingEye(*third->wall, {6.f, -20.f, 4.f}, palette);
  const auto near_high = FacingEye(*third->wall, {6.f, 30.f, 4.f}, palette);
  ASSERT_TRUE(near_low && near_high);
  EXPECT_LT(near_low->wall.thickness * near_high->wall.thickness, 0);
  EXPECT_NE(near_low->blocks.front().cell, near_high->blocks.front().cell);
}

// A bent draft after its third click: a stick of 8 along z at the origin, the second node at (12, 4, 0).
WallDraft BentDraftAtTheTip() {
  return {.stage = WallDraft::Stage::kTip, .first = kOrigin, .second = {12, 4, 0}, .stick_axis = WallAxis::kZ,
          .stick_length = 8, .bent = true};
}

// From straight above the second node, down onto the floor at (12, 10): a stick of 6 along y.
Aim TipRay() {
  return RayThrough({12.f, 10.f, 6.f}, {12.f, 10.f, 0.f});
}

TEST(WallBrushTest, TheTipIsTheStickAlongTheAxisThePickedPointIsFurthestOn) {
  const Eigen::Vector3i second {12, 4, 0};

  const auto along_y = PickTip(TipRay(), second, {}, kPicking);
  ASSERT_TRUE(along_y.has_value());
  EXPECT_EQ(along_y->axis, WallAxis::kY);
  EXPECT_EQ(along_y->length, 6);

  const auto backwards = PickTip(RayThrough({9.f, 3.f, 6.f}, {9.f, 3.f, 0.f}), second, {}, kPicking);
  ASSERT_TRUE(backwards.has_value());
  EXPECT_EQ(backwards->axis, WallAxis::kX);
  EXPECT_EQ(backwards->length, -3);

  // Straight down onto the node itself: no stick.
  EXPECT_FALSE(PickTip(RayThrough({12.f, 4.f, 6.f}, {12.f, 4.f, 0.f}), second, {}, kPicking).has_value());
}

TEST(WallBrushTest, ABentDraftTakesAFourthClickForTheStickAtTheSecondNode) {
  const std::vector<Eigen::Vector3i> none;
  WallDraft height {.stage = WallDraft::Stage::kHeight, .first = kOrigin, .second = {12, 4, 0},
                    .stick_axis = WallAxis::kZ, .bent = true};

  const auto third = ClickWall(height, RayThrough({30.f, 0.f, 8.f}, {20.f, 0.f, 8.f}), none, kPicking, kThickness);
  ASSERT_TRUE(third && third->draft && !third->wall);
  EXPECT_EQ(third->draft->stage, WallDraft::Stage::kTip);
  EXPECT_EQ(third->draft->stick_length, 8);

  const auto fourth = ClickWall(third->draft, TipRay(), none, kPicking, kThickness);
  ASSERT_TRUE(fourth && fourth->wall);
  EXPECT_EQ(fourth->wall->node_a, kOrigin);
  EXPECT_EQ(fourth->wall->node_b, Eigen::Vector3i(12, 4, 0));
  EXPECT_EQ(fourth->wall->stick_a, (z13::station::WallStick {WallAxis::kZ, 8}));
  EXPECT_EQ(fourth->wall->stick_b, (z13::station::WallStick {WallAxis::kY, 6}));
  EXPECT_EQ(fourth->wall->diagonal, z13::building::primitives::DefaultDiagonal(*fourth->wall));
}

TEST(WallBrushTest, AFlippedDraftTakesTheOtherDiagonal) {
  const std::vector<Eigen::Vector3i> none;
  WallDraft draft = BentDraftAtTheTip();
  const auto plain = ClickWall(draft, TipRay(), none, kPicking, kThickness);
  draft.flipped = true;
  const auto flipped = ClickWall(draft, TipRay(), none, kPicking, kThickness);

  ASSERT_TRUE(plain && plain->wall && flipped && flipped->wall);
  EXPECT_EQ(flipped->wall->diagonal, z13::building::primitives::Flipped(plain->wall->diagonal));
}

TEST(WallBrushTest, ATipOnTheFirstSticksAxisMakesATrapezoid) {
  const std::vector<Eigen::Vector3i> none;
  // From the side onto the y plane through the second node: a stick of 5 along z.
  const auto click = ClickWall(
      BentDraftAtTheTip(), RayThrough({12.f, -10.f, 5.f}, {12.f, 4.f, 5.f}), none, kPicking, kThickness);

  ASSERT_TRUE(click && click->wall);
  EXPECT_EQ(click->wall->stick_b, (z13::station::WallStick {WallAxis::kZ, 5}));
  EXPECT_FALSE(z13::building::primitives::IsBent(*click->wall));
}

TEST(WallBrushTest, TheTipPreviewDrawsBothSticksAndShowsTheBentWall) {
  const auto palette = z13::testing::ShippedPalette().value();

  const WallPreview preview = PreviewWall(BentDraftAtTheTip(), TipRay(), {}, kPicking, kThickness, palette);

  EXPECT_TRUE(preview.makeable);
  ASSERT_TRUE(preview.wall.has_value());
  EXPECT_EQ(preview.parts.size(), 1u);
  ASSERT_EQ(preview.lines.size(), 2u);
  EXPECT_EQ(preview.lines[0].to, Eigen::Vector3f(0.f, 0.f, 8.f));
  EXPECT_EQ(preview.lines[1].from, Eigen::Vector3f(12.f, 4.f, 0.f));
  EXPECT_EQ(preview.lines[1].to, Eigen::Vector3f(12.f, 10.f, 0.f));
}

TEST(WallBrushTest, ABentDraftsHeightClickPreviewsTheSticksAndLaysNoWall) {
  const auto palette = z13::testing::ShippedPalette().value();
  const WallDraft draft {.stage = WallDraft::Stage::kHeight, .first = kOrigin, .second = {12, 4, 0},
                         .stick_axis = WallAxis::kZ, .bent = true};

  const WallPreview preview =
      PreviewWall(draft, RayThrough({30.f, 0.f, 8.f}, {20.f, 0.f, 8.f}), {}, kPicking, kThickness, palette);

  EXPECT_TRUE(preview.makeable);
  EXPECT_FALSE(preview.wall.has_value());
  EXPECT_EQ(preview.lines.size(), 2u);
}

TEST(WallBrushTest, TheStickPreviewDrawsTheStickAndTheArrowAlongTheLeftOverAxis) {
  const auto palette = z13::testing::ShippedPalette().value();
  const WallDraft draft {.stage = WallDraft::Stage::kStick, .first = kOrigin};
  const WallPreview preview =
      PreviewWall(draft, DownRay(), {}, kPicking, kThickness, palette);
  EXPECT_TRUE(preview.makeable);
  EXPECT_EQ(preview.block.cell, Eigen::Vector3i(4, 1, 0));
  ASSERT_EQ(preview.lines.size(), 4u);
  EXPECT_EQ(preview.lines[0].to, kFloorTarget);
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
      .node_a = {0, 0, 0}, .node_b = {12, 4, 0}, .stick_a = {WallAxis::kZ, 8}, .stick_b = {WallAxis::kZ, 8},
      .thickness = 2};
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
