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

#include <cstdint>

#include <Eigen/Core>

#include <z13/components/block.h>
#include <z13/components/block_brush.h>
#include <touch/touch_mapping.h>
#include <z13/components/building.h>
#include <z13/components/input_events.h>
#include <z13/components/inventory.h>
#include <z13/components/player.h>
#include <z13/components/touch_controls.h>
#include <z13/components/wall.h>
#include <primitives/placement.h>
#include <z13_settings/settings.h>

#include "../../z13_module/tests/support/building_test_helpers.h"
#include "../../z13_module/tests/support/z13_test_world.h"
#include "support/station_builders.h"

// The build tool as an item: its modes, the held cut keys, the destroy button and the brush
// it keeps (docs/android-plan.md, "Строительный инструмент как предмет").
namespace z13::building {
namespace {

using Keycode = z13::fbs::input::Keycode;
using z13::station::BlockBrush;
using z13::station::PaletteChoice;
using z13::testing::AddWallAhead;
using z13::testing::BlocksOfType;
using z13::testing::BuildToolOf;
using z13::testing::kSlopeId;
using z13::testing::kSlopeSlot;
using z13::testing::kToolSettleTicks;
using z13::testing::kWallId;
using z13::testing::LocalBuilder;
using z13::testing::StationWorld;
using z13::testing::Tap;
using z13::testing::Ticks;
using z13::testing::TickOnly;
using z13::testing::Z13TestWorld;

// CYCLE_TOOL_MODE's, CUT_MODIFIER's and CUT_IN_MODIFIER's default keys (building.fbs).
constexpr Keycode kModeKey = Keycode::KEY_V;
constexpr Keycode kCutKey = Keycode::KEY_X;
constexpr Keycode kCutInKey = Keycode::KEY_LSHIFT;

ToolMode ModeOf(flecs::entity player) {
  return BuildToolOf(player).get<BuildTool>().mode;
}

const Eigen::Vector2f kScreen(1000.f, 500.f);
constexpr uint64_t kFinger = 1;

template <typename EventT>
EventT Touch(const Eigen::Vector2f& position) {
  EventT event;
  event.finger = kFinger;
  event.position = position;
  event.screen_size = kScreen;
  return event;
}

void TapButton(Z13TestWorld& test_world, z13::input::TouchControl control) {
  const Eigen::Vector2f center = z13::input::touch::ButtonCenter(
      test_world.World().get<z13::TouchLayout>(), test_world.World().get<z13::input::TouchControls>().layer, control,
      kScreen);
  test_world.EmitInput(Touch<z13::input::TouchDownEvent>(center));
  test_world.Tick();
  test_world.EmitInput(Touch<z13::input::TouchUpEvent>(center));
  test_world.Tick();
}

TEST(BuildToolTest, TheModeKeyCyclesTheModes) {
  Z13TestWorld test_world = StationWorld();
  const flecs::entity player = LocalBuilder(test_world);
  ASSERT_EQ(ModeOf(player), ToolMode::kBuild);

  for (const ToolMode expected : {ToolMode::kDestroy, ToolMode::kCut, ToolMode::kCutIn, ToolMode::kWall, ToolMode::kMerge, ToolMode::kBuild}) {
    Tap(test_world, kModeKey, TickOnly(test_world));
    EXPECT_EQ(ModeOf(player), expected);
  }
}

TEST(BuildToolTest, InDestroyModeThePrimaryButtonDestroys) {
  Z13TestWorld test_world = StationWorld();
  const flecs::entity player = LocalBuilder(test_world);
  AddWallAhead(test_world, player);
  const size_t walls = BlocksOfType(test_world.World(), kWallId).size();

  Tap(test_world, kModeKey, TickOnly(test_world));
  z13::testing::Click(test_world, Keycode::MOUSE_BUTTON_LEFT);

  EXPECT_EQ(BlocksOfType(test_world.World(), kWallId).size(), walls - 1);
}

TEST(BuildToolTest, OnATouchScreenTheModeAndActionButtonsDestroyLikeTheKeys) {
  Z13TestWorld test_world = StationWorld();
  const flecs::entity player = LocalBuilder(test_world);
  AddWallAhead(test_world, player);
  const size_t walls = BlocksOfType(test_world.World(), kWallId).size();

  TapButton(test_world, z13::input::TouchControl::kToolMode);
  ASSERT_EQ(ModeOf(player), ToolMode::kDestroy);
  TapButton(test_world, z13::input::TouchControl::kPrimary);

  EXPECT_EQ(BlocksOfType(test_world.World(), kWallId).size(), walls - 1);
}

TEST(BuildToolTest, TheDestroyButtonDestroysInAnyMode) {
  Z13TestWorld test_world = StationWorld();
  const flecs::entity player = LocalBuilder(test_world);
  AddWallAhead(test_world, player);
  const size_t walls = BlocksOfType(test_world.World(), kWallId).size();
  Tap(test_world, kModeKey, TickOnly(test_world));
  Tap(test_world, kModeKey, TickOnly(test_world));
  ASSERT_EQ(ModeOf(player), ToolMode::kCut);

  z13::testing::Click(test_world, Keycode::MOUSE_BUTTON_RIGHT);

  EXPECT_EQ(BlocksOfType(test_world.World(), kWallId).size(), walls - 1);
}

TEST(BuildToolTest, AHeldCutKeyOverridesTheModeWhileHeld) {
  Z13TestWorld test_world = StationWorld();
  const flecs::entity player = LocalBuilder(test_world);
  Tap(test_world, kModeKey, TickOnly(test_world));
  ASSERT_EQ(ModeOf(player), ToolMode::kDestroy);

  test_world.EmitInput(z13::testing::KeyDown(kCutInKey));
  Ticks(test_world, 2);
  EXPECT_TRUE(player.has<CutInModifier>());
  test_world.EmitInput(z13::testing::KeyDown(kCutKey));
  Ticks(test_world, 2);
  EXPECT_TRUE(player.has<CutModifier>());
  EXPECT_FALSE(player.has<CutInModifier>());

  test_world.EmitInput(z13::testing::KeyUp(kCutKey));
  test_world.EmitInput(z13::testing::KeyUp(kCutInKey));
  Ticks(test_world, 2);
  EXPECT_FALSE(player.has<CutModifier>());
  EXPECT_FALSE(player.has<CutInModifier>());
  EXPECT_EQ(ModeOf(player), ToolMode::kDestroy);
}

TEST(BuildToolTest, TheCutModeCutsLikeTheHeldKey) {
  Z13TestWorld test_world = StationWorld();
  const flecs::entity player = LocalBuilder(test_world);

  Tap(test_world, kModeKey, TickOnly(test_world));
  Tap(test_world, kModeKey, TickOnly(test_world));

  EXPECT_TRUE(player.has<CutModifier>());
}

TEST(BuildToolTest, TheBrushStaysWithTheToolWhileItIsPutAway) {
  Z13TestWorld test_world = StationWorld();
  const flecs::entity player = LocalBuilder(test_world);
  test_world.World().set(PaletteChoice {.slot = kSlopeSlot});
  Ticks(test_world, 2);
  ASSERT_EQ(BuildToolOf(player).get<BlockBrush>().spec.type_id, kSlopeId);

  z13::testing::ToggleBuildMode(test_world);
  Ticks(test_world, kToolSettleTicks);
  ASSERT_FALSE(player.has<BuildingTool>());
  EXPECT_FALSE(player.has<BlockBrush>());

  z13::testing::ToggleBuildMode(test_world);
  Ticks(test_world, kToolSettleTicks);
  EXPECT_TRUE(player.has<BuildingTool>());
  EXPECT_EQ(BuildToolOf(player).get<BlockBrush>().spec.type_id, kSlopeId);
}

TEST(BuildToolTest, AnotherCellPutsTheToolAwayAndEndsADrag) {
  Z13TestWorld test_world = StationWorld();
  const flecs::entity player = LocalBuilder(test_world);
  test_world.EmitInput(z13::testing::MouseDown(Keycode::MOUSE_BUTTON_LEFT));
  Ticks(test_world, 2);
  ASSERT_TRUE(player.has<z13::station::BrushDrag>());

  Tap(test_world, Keycode::KEY_3, TickOnly(test_world));
  test_world.EmitInput(z13::testing::MouseUp(Keycode::MOUSE_BUTTON_LEFT));
  Ticks(test_world, 2);

  EXPECT_FALSE(player.has<BuildingTool>());
  EXPECT_FALSE(player.has<z13::station::BrushDrag>());
}

TEST(BuildToolTest, OnATouchScreenThePanelPicksAndTurnsTheBrushLikeTheKeys) {
  Z13TestWorld test_world = StationWorld();
  const flecs::entity player = LocalBuilder(test_world);
  const z13::station::BlockSpec before = BuildToolOf(player).get<BlockBrush>().spec;

  TapButton(test_world, z13::input::TouchControl::kNextPrimitive);
  TapButton(test_world, z13::input::TouchControl::kTurnZ);

  const z13::station::BlockSpec after = BuildToolOf(player).get<BlockBrush>().spec;
  EXPECT_NE(after.type_id, before.type_id);
  EXPECT_EQ(after.orientation,
            z13::building::primitives::QuarterTurn(before.orientation, z13::building::primitives::TurnAxis::kZ));
}

z13::input::TouchLayer LayerOf(Z13TestWorld& test_world) {
  return test_world.World().get<z13::input::TouchControls>().layer;
}

TEST(BuildToolTest, TheTouchLayerFollowsTheHeldToolsMode) {
  Z13TestWorld test_world = StationWorld();
  const flecs::entity player = LocalBuilder(test_world);
  test_world.Tick();
  EXPECT_EQ(LayerOf(test_world), z13::input::TouchLayer::kBlock);

  for (const auto& [mode, layer] : {std::pair {ToolMode::kWall, z13::input::TouchLayer::kWall},
                                    std::pair {ToolMode::kMerge, z13::input::TouchLayer::kMerge},
                                    std::pair {ToolMode::kDestroy, z13::input::TouchLayer::kBlock}}) {
    BuildToolOf(player).get_mut<BuildTool>().mode = mode;
    test_world.Tick();
    EXPECT_EQ(LayerOf(test_world), layer);
  }
}

TEST(BuildToolTest, TheWallLayersButtonsSetTheThickness) {
  Z13TestWorld test_world = StationWorld();
  const flecs::entity player = LocalBuilder(test_world);
  BuildToolOf(player).get_mut<BuildTool>().mode = ToolMode::kWall;
  test_world.Tick();

  TapButton(test_world, z13::input::TouchControl::kThicker);
  EXPECT_EQ(BuildToolOf(player).get<BuildTool>().wall_thickness, 2);
  TapButton(test_world, z13::input::TouchControl::kThinner);
  EXPECT_EQ(BuildToolOf(player).get<BuildTool>().wall_thickness, 1);
}

TEST(BuildToolTest, TheMergeLayersButtonStepsTheMergeTarget) {
  Z13TestWorld test_world = StationWorld();
  const flecs::entity player = LocalBuilder(test_world);
  BuildToolOf(player).get_mut<BuildTool>().mode = ToolMode::kMerge;
  test_world.Tick();
  const MergeAt before = BuildToolOf(player).get<BuildTool>().merge_at;

  TapButton(test_world, z13::input::TouchControl::kMergeAt);

  EXPECT_NE(BuildToolOf(player).get<BuildTool>().merge_at, before);
}

TEST(BuildToolTest, TheDestroyButtonDropsAWallInTheMakingElseDestroys) {
  Z13TestWorld test_world = StationWorld();
  const flecs::entity player = LocalBuilder(test_world);
  AddWallAhead(test_world, player);
  const size_t walls = BlocksOfType(test_world.World(), kWallId).size();
  BuildToolOf(player).get_mut<BuildTool>().mode = ToolMode::kWall;
  test_world.Tick();
  player.set(z13::station::WallDraft {});

  TapButton(test_world, z13::input::TouchControl::kDestroy);
  EXPECT_FALSE(player.has<z13::station::WallDraft>());
  EXPECT_EQ(BlocksOfType(test_world.World(), kWallId).size(), walls);

  TapButton(test_world, z13::input::TouchControl::kDestroy);
  EXPECT_EQ(BlocksOfType(test_world.World(), kWallId).size(), walls - 1);
}

}  // namespace
}  // namespace z13::building
