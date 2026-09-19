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

#include <Eigen/Dense>

#include <z13/components/building.h>
#include <z13/components/input.h>

#include "../support/building_test_helpers.h"
#include "../support/z13_test_world.h"

// The brush and the Building input group are derived from the BuildingTool tag
// by systems, so they must follow the tag however it changes -- toggled from
// input, or added/removed directly as state restore does.
namespace z13::building {
namespace {

using z13::testing::Click;
using z13::testing::kTestDeltaTime;

void ProgressFrames(z13::testing::Z13TestWorld& test_world, int frames) {
  for (int i = 0; i < frames; ++i) {
    test_world.World().progress(kTestDeltaTime);
  }
}

size_t ActionGroupCount(flecs::entity player) {
  return player.get<z13::input::ActionListener>().action_group_priority.size();
}

TEST(BuildingModeTest, AddingBuildingToolCreatesSingleBrushAndInputGroup) {
  z13::testing::Z13TestWorld test_world;
  flecs::entity player = test_world.Player();
  const size_t groups_before = ActionGroupCount(player);

  player.add<BuildingTool>();
  ProgressFrames(test_world, 3);

  EXPECT_EQ(test_world.World().count<Brush>(), 1);
  EXPECT_EQ(ActionGroupCount(player), groups_before + 1);
}

TEST(BuildingModeTest, RemovingBuildingToolRemovesBrushAndInputGroup) {
  z13::testing::Z13TestWorld test_world;
  flecs::entity player = test_world.Player();
  const size_t groups_before = ActionGroupCount(player);
  player.add<BuildingTool>();
  ProgressFrames(test_world, 2);

  player.remove<BuildingTool>();
  ProgressFrames(test_world, 2);

  EXPECT_EQ(test_world.World().count<Brush>(), 0);
  EXPECT_EQ(ActionGroupCount(player), groups_before);
}

TEST(BuildingModeTest, BrushWithoutBuildingToolIsRemoved) {
  z13::testing::Z13TestWorld test_world;
  test_world.World().entity().child_of(test_world.Player()).set(Brush{.distance = 1.f});

  ProgressFrames(test_world, 1);

  EXPECT_EQ(test_world.World().count<Brush>(), 0);
}

TEST(BuildingModeTest, DuplicateBrushesAreCollapsedToOne) {
  z13::testing::Z13TestWorld test_world;
  flecs::entity player = test_world.Player();
  player.add<BuildingTool>();
  ProgressFrames(test_world, 2);

  test_world.World().entity().child_of(player).set(Brush{.distance = 1.f});
  ProgressFrames(test_world, 2);

  EXPECT_EQ(test_world.World().count<Brush>(), 1);
}

TEST(BuildingModeTest, BuildingToolAddedDirectlyEnablesBuildInput) {
  z13::testing::Z13TestWorld test_world;
  test_world.Player().add<BuildingTool>();
  ProgressFrames(test_world, 2);

  Click(test_world, z13::fbs::input::Keycode::MOUSE_BUTTON_LEFT);

  EXPECT_EQ(test_world.World().count<BasicBlock>(), 1);
}

TEST(BuildingModeTest, LeavingBuildModeRemovesBrushInTheSameFrame) {
  z13::testing::Z13TestWorld test_world;
  z13::testing::EnterBuildMode(test_world);
  ASSERT_EQ(test_world.World().count<Brush>(), 1);

  z13::testing::ToggleBuildMode(test_world);

  EXPECT_EQ(test_world.World().count<Brush>(), 0);
}

// A system running late in the frame (like the render phase) must already see the
// removal: this holds only if the removing systems declare what they read/write, so
// flecs merges their commands in between.
TEST(BuildingModeTest, LateSystemsSeeTheBrushRemovalInTheSameFrame) {
  z13::testing::Z13TestWorld test_world;
  z13::testing::EnterBuildMode(test_world);
  int seen_by_late_system = -1;
  test_world.World().system("Test::LateBrushReader").kind(flecs::OnStore).read<Brush>().run(
      [&seen_by_late_system](flecs::iter& it) {
        while (it.next()) {
          seen_by_late_system = it.world().count<Brush>();
        }
      });

  z13::testing::ToggleBuildMode(test_world);

  EXPECT_EQ(seen_by_late_system, test_world.World().count<Brush>());
  EXPECT_EQ(seen_by_late_system, 0);
}

// The brush sits 5 units in front of the player, so this puts it exactly at the origin.
TEST(BuildingModeTest, BrushAtTheWorldOriginStillGetsATransform) {
  z13::testing::Z13TestWorld test_world;
  Eigen::Matrix4f player_transform = Eigen::Matrix4f::Identity();
  player_transform(0, 3) = -5.f;
  test_world.Player().set(player_transform);

  z13::testing::EnterBuildMode(test_world);
  Click(test_world, z13::fbs::input::Keycode::MOUSE_BUTTON_LEFT);

  EXPECT_EQ(test_world.World().count<BasicBlock>(), 1);
}

TEST(BuildingModeTest, EnteringBuildModeFromInputCreatesBrush) {
  z13::testing::Z13TestWorld test_world;

  z13::testing::EnterBuildMode(test_world);

  EXPECT_EQ(test_world.World().count<Brush>(), 1);
}

}  // namespace
}  // namespace z13::building
