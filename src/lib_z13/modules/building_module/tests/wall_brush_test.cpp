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

#include <vector>

#include <Eigen/Dense>

#include <lib_core/utils/math.h>
#include <z13/components/block.h>
#include <z13/components/building.h>
#include <z13/components/player_parts.h>
#include <z13/components/wall.h>
#include <z13_tests/shipped_station.h>

#include "../../z13_module/tests/support/building_test_helpers.h"
#include "../../z13_module/tests/support/z13_test_world.h"
#include "../src/wall_brush.h"
#include "support/station_builders.h"

// The Wall mode's three clicks: snapping, the draft, the side the wall thickens to.
namespace z13::building {
namespace {

using z13::station::Block;
using z13::station::WallAxis;
using z13::station::WallDraft;
using z13::station::WallPanelOf;
using z13::station::WallSegment;
using z13::testing::AddBuilder;
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

TEST(WallBrushTest, TheSecondClickSetsTheStickAlongTheLongestOffset) {
  const auto first = ClickWall(std::nullopt, {2, 3, 4}, 1);
  ASSERT_TRUE(first && first->draft);
  EXPECT_EQ(first->draft->stage, WallDraft::Stage::kStick);

  const auto second = ClickWall(first->draft, {2, 3, -4}, 1);
  ASSERT_TRUE(second && second->draft);
  EXPECT_EQ(second->draft->stick_axis, WallAxis::kZ);
  EXPECT_EQ(second->draft->stick_length, -8);

  EXPECT_FALSE(ClickWall(first->draft, {2, 3, 4}, 1).has_value());
}

TEST(WallBrushTest, TheThirdClickMakesTheWallAndItThickensTowardTheEye) {
  const auto palette = z13::testing::ShippedPalette().value();
  const WallDraft draft {.stage = WallDraft::Stage::kEnd, .first = {0, 0, 0}, .stick_axis = WallAxis::kZ,
                         .stick_length = 8};
  const auto click = ClickWall(draft, {12, 4, 0}, 2);
  ASSERT_TRUE(click && click->wall);

  const auto near_low = FacingEye(*click->wall, {6.f, -20.f, 4.f}, palette);
  const auto near_high = FacingEye(*click->wall, {6.f, 30.f, 4.f}, palette);
  ASSERT_TRUE(near_low && near_high);
  EXPECT_LT(near_low->wall.thickness * near_high->wall.thickness, 0);
  EXPECT_NE(near_low->block.cell, near_high->block.cell);
}

TEST(WallBrushTest, ThreeClicksLayASegmentWithItsPanel) {
  Z13TestWorld test_world = StationWorld();
  const flecs::entity builder = AddBuilder(test_world, kBuilderId, Facing(kBuilderPosition, Eigen::Vector3f::UnitX()));
  BuildTool& tool = z13::testing::BuildToolOf(builder).get_mut<BuildTool>();
  tool.mode = ToolMode::kWall;
  // A rise of 4 over a run of 12 needs two cells to be airtight.
  tool.wall_thickness = 2;
  // The wall passes the builder, who walks with the brush: no player to keep clear of.
  builder.get_mut<z13::gameplay::PlayerCollider>().radius = 0.f;

  const auto click_after_moving = [&](const Eigen::Vector3f& offset) {
    Eigen::Matrix4f transform = builder.get<Eigen::Matrix4f>();
    z13::math::SetTranslation(
        Eigen::Vector3f(z13::math::ExtractTranslation<float>(transform) + offset), transform);
    builder.set(transform);
    builder.add<RequestBuildBlock>();
    test_world.Tick();
  };
  click_after_moving(Eigen::Vector3f::Zero());
  ASSERT_TRUE(builder.has<WallDraft>());
  click_after_moving({0.f, 0.f, 2.f});
  EXPECT_EQ(builder.get<WallDraft>().stage, WallDraft::Stage::kEnd);
  click_after_moving({3.f, 1.f, -2.f});
  test_world.Tick();

  EXPECT_FALSE(builder.has<WallDraft>());
  EXPECT_EQ(test_world.World().count<WallSegment>(), 1);
  EXPECT_EQ(test_world.World().count<WallPanelOf>(), 1);
}

}  // namespace
}  // namespace z13::building
