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
#include <memory>
#include <optional>
#include <string>

#include <Eigen/Dense>

#include <lib_core/state/world_json_store.h>
#include <lib_core/utils/math.h>
#include <net_module/in_memory_transport.h>
#include <z13/components/building.h>
#include <z13/components/gameplay.h>
#include <z13/components/station.h>
#include <z13_module/gameplay/gameplay_entities.h>
#include <z13_primitives/palette.h>
#include <z13_primitives/placement.h>

#include "../../z13_module/tests/support/building_test_helpers.h"
#include "../../z13_module/tests/support/test_network.h"
#include "../../z13_module/tests/support/world_json_test_helpers.h"
#include "../../z13_module/tests/support/z13_test_world.h"
#include "support/station_builders.h"

// The brush's pick, turn and drag through real input, and the build permission.
namespace z13::building {
namespace {

using z13::building::primitives::QuarterTurn;
using z13::building::primitives::TurnAxis;
using z13::station::Block;
using z13::station::BlockBrush;
using z13::station::BlockSpec;
using z13::station::BrushPreview;
using z13::station::BuildPermission;
using z13::station::Orientation;
using z13::station::PaletteChoice;
using z13::testing::AddBuilder;
using z13::testing::BlocksOfType;
using z13::testing::Facing;
using z13::testing::kConnectArg;
using z13::testing::kMaxNetTestTicks;
using z13::testing::kNetTestDeltaTime;
using z13::testing::kServerArg;
using z13::testing::kStationArg;
using z13::testing::kTestServerEndpoint;
using z13::testing::RunNetworkUntil;
using z13::testing::StationWorld;
using z13::testing::Tap;
using z13::testing::TickOnly;
using z13::testing::Z13TestWorld;
using Keycode = z13::fbs::input::Keycode;

// assets/station/palette.json, in its order.
constexpr uint32_t kFloorId = 1;
constexpr uint32_t kWallId = 2;
constexpr uint32_t kDoorId = 3;
constexpr uint32_t kWindowId = 4;
constexpr uint32_t kSlopeId = 5;
constexpr size_t kSlopeSlot = 4;
constexpr uint32_t kBuilderId = 7;
// Off the cell boundary at y = 0, so the brush and the destroy ray agree on the row.
const Eigen::Vector3f kBuilderPosition {0.f, 0.1f, 1.25f};
// The Building action group joins a frame after the tool is out.
constexpr int kToolSettleTicks = 3;

void Ticks(Z13TestWorld& test_world, int count) {
  for (int i = 0; i < count; ++i) {
    test_world.Tick();
  }
}

flecs::entity LocalBuilder(Z13TestWorld& test_world) {
  test_world.Tick();
  z13::testing::EnterBuildMode(test_world);
  Ticks(test_world, kToolSettleTicks);
  return test_world.Player();
}

BlockSpec BrushOf(flecs::entity player) {
  return player.get<BlockBrush>().spec;
}

std::optional<BrushPreview> PreviewOf(flecs::entity player) {
  std::optional<BrushPreview> preview;
  player.children([&preview](flecs::entity child) {
    if (child.has<BrushPreview>()) {
      preview = child.get<BrushPreview>();
    }
  });
  return preview;
}

void MovePlayerBy(flecs::entity player, const Eigen::Vector3f& offset) {
  Eigen::Matrix4f transform = player.get<Eigen::Matrix4f>();
  z13::math::SetTranslation(Eigen::Vector3f(z13::math::ExtractTranslation<float>(transform) + offset), transform);
  player.set(transform);
}

TEST(BrushTest, SlotAndStepKeysPickThePrimitive) {
  Z13TestWorld test_world = StationWorld();
  const flecs::entity player = LocalBuilder(test_world);

  Tap(test_world, Keycode::KEY_1, TickOnly(test_world));
  EXPECT_EQ(BrushOf(player).type_id, kFloorId);
  // A picked primitive spans 2 m along the axes it stretches along.
  EXPECT_EQ(BrushOf(player).size, Eigen::Vector3i(8, 8, 1));

  Tap(test_world, Keycode::KEY_E, TickOnly(test_world));
  EXPECT_EQ(BrushOf(player).type_id, kWallId);
  EXPECT_EQ(BrushOf(player).size, Eigen::Vector3i(8, 1, 8));

  // Back past the first primitive wraps around to the last.
  Tap(test_world, Keycode::KEY_Q, TickOnly(test_world));
  Tap(test_world, Keycode::KEY_Q, TickOnly(test_world));
  const auto& primitives = test_world.World().get<z13::building::primitives::BlockPalette>().palette.primitives;
  EXPECT_EQ(BrushOf(player).type_id, primitives.back().id);
}

TEST(BrushTest, TurnKeysTurnTheBrush) {
  Z13TestWorld test_world = StationWorld();
  const flecs::entity player = LocalBuilder(test_world);

  Tap(test_world, Keycode::KEY_R, TickOnly(test_world));
  EXPECT_EQ(BrushOf(player).orientation, QuarterTurn(Orientation {}, TurnAxis::kZ));
  Tap(test_world, Keycode::KEY_G, TickOnly(test_world));
  EXPECT_EQ(BrushOf(player).orientation, QuarterTurn(QuarterTurn(Orientation {}, TurnAxis::kZ), TurnAxis::kX));
}

TEST(BrushTest, ThePaletteWindowsPickReachesTheBrush) {
  Z13TestWorld test_world = StationWorld();
  const flecs::entity player = LocalBuilder(test_world);

  test_world.World().set(PaletteChoice {.slot = kSlopeSlot});
  Ticks(test_world, 2);

  EXPECT_EQ(BrushOf(player).type_id, kSlopeId);
  EXPECT_FALSE(test_world.World().get<PaletteChoice>().slot.has_value());
}

// The builder walks 1 m (4 cells) along +X between press and release.
TEST(BrushTest, ADragSizesTheWallAndTheNextClickRepeatsIt) {
  Z13TestWorld test_world = StationWorld();
  const flecs::entity player = LocalBuilder(test_world);

  test_world.EmitInput(z13::testing::MouseDown(Keycode::MOUSE_BUTTON_LEFT));
  test_world.Tick();
  MovePlayerBy(player, {1.f, 0.f, 0.f});
  Ticks(test_world, 2);
  const auto preview = PreviewOf(player);
  ASSERT_TRUE(preview.has_value());
  EXPECT_EQ(preview->block.spec.size, Eigen::Vector3i(4, 1, 8));
  EXPECT_TRUE(BlocksOfType(test_world.World(), kWallId).empty());

  test_world.EmitInput(z13::testing::MouseUp(Keycode::MOUSE_BUTTON_LEFT));
  Ticks(test_world, 2);
  ASSERT_EQ(BlocksOfType(test_world.World(), kWallId).size(), 1u);
  EXPECT_EQ(BlocksOfType(test_world.World(), kWallId)[0].spec.size, Eigen::Vector3i(4, 1, 8));
  EXPECT_EQ(BrushOf(player).size, Eigen::Vector3i(4, 1, 8));

  MovePlayerBy(player, {0.f, 3.f, 0.f});
  z13::testing::Click(test_world, Keycode::MOUSE_BUTTON_LEFT);
  test_world.Tick();
  const auto walls = BlocksOfType(test_world.World(), kWallId);
  ASSERT_EQ(walls.size(), 2u);
  EXPECT_EQ(walls[1].spec.size, Eigen::Vector3i(4, 1, 8));
}

TEST(BrushTest, ThePaletteWindowsPickLandsWithTheToolPutAway) {
  Z13TestWorld test_world = StationWorld();
  test_world.Tick();

  test_world.World().set(PaletteChoice {.slot = kSlopeSlot});
  Ticks(test_world, 2);

  EXPECT_EQ(BrushOf(test_world.Player()).type_id, kSlopeId);
}

// Pausing zeroes the input, which reads as the release that would place the block.
TEST(BrushTest, PausingMidDragPlacesNothing) {
  Z13TestWorld test_world = StationWorld();
  const flecs::entity player = LocalBuilder(test_world);
  test_world.EmitInput(z13::testing::MouseDown(Keycode::MOUSE_BUTTON_LEFT));
  test_world.Tick();
  MovePlayerBy(player, {1.f, 0.f, 0.f});
  test_world.Tick();
  ASSERT_TRUE(player.has<z13::station::BrushDrag>());

  test_world.World().add<z13::gameplay::Pause>();
  Ticks(test_world, 2);
  test_world.EmitInput(z13::testing::MouseUp(Keycode::MOUSE_BUTTON_LEFT));
  Ticks(test_world, 2);
  test_world.World().remove<z13::gameplay::Pause>();
  Ticks(test_world, 2);

  EXPECT_TRUE(BlocksOfType(test_world.World(), kWallId).empty());
  EXPECT_FALSE(player.has<z13::station::BrushDrag>());
}

TEST(BrushTest, ThePreviewShowsWhetherTheBuildWouldBeAccepted) {
  Z13TestWorld test_world = StationWorld();
  const flecs::entity builder = AddBuilder(test_world, kBuilderId, Facing(kBuilderPosition, Eigen::Vector3f::UnitX()));
  ASSERT_TRUE(PreviewOf(builder).has_value());
  EXPECT_TRUE(PreviewOf(builder)->valid);

  builder.add<RequestBuildBlock>();
  test_world.Tick();
  ASSERT_EQ(BlocksOfType(test_world.World(), kWallId).size(), 1u);
  // The brush aims at the first block's face now, so the preview rests against it.
  EXPECT_TRUE(PreviewOf(builder)->valid);
  builder.remove<z13::station::BuildPermission>();
  test_world.Tick();
  EXPECT_FALSE(PreviewOf(builder)->valid);
}

TEST(BrushTest, WithoutPermissionNothingIsBuiltOrDestroyed) {
  Z13TestWorld test_world = StationWorld();
  const flecs::entity builder = AddBuilder(test_world, kBuilderId, Facing(kBuilderPosition, Eigen::Vector3f::UnitX()));
  builder.remove<BuildPermission>();
  test_world.Tick();
  EXPECT_FALSE(PreviewOf(builder)->valid);

  builder.add<RequestBuildBlock>();
  test_world.Tick();
  EXPECT_TRUE(BlocksOfType(test_world.World(), kWallId).empty());

  builder.add<BuildPermission>();
  builder.add<RequestBuildBlock>();
  test_world.Tick();
  ASSERT_EQ(BlocksOfType(test_world.World(), kWallId).size(), 1u);
  builder.remove<BuildPermission>();
  builder.add<RequestDestroyBlock>();
  test_world.Tick();
  EXPECT_EQ(BlocksOfType(test_world.World(), kWallId).size(), 1u);
}

TEST(BrushTest, DoorsAndWindowsAreDestroyedWhole) {
  for (const BlockSpec spec : {BlockSpec {.type_id = kDoorId, .size = {6, 1, 10}},
                               BlockSpec {.type_id = kWindowId, .size = {8, 1, 8}}}) {
    Z13TestWorld test_world = StationWorld();
    const flecs::entity builder =
        AddBuilder(test_world, kBuilderId, Facing(kBuilderPosition, Eigen::Vector3f::UnitX()));
    builder.set(BlockBrush {.spec = spec});
    builder.add<RequestBuildBlock>();
    test_world.Tick();
    ASSERT_EQ(BlocksOfType(test_world.World(), spec.type_id).size(), 1u) << spec.type_id;

    builder.add<RequestDestroyBlock>();
    test_world.Tick();
    EXPECT_TRUE(BlocksOfType(test_world.World(), spec.type_id).empty()) << spec.type_id;
  }
}

std::string Checkpoint(Z13TestWorld& test_world) {
  const auto json = z13::flecs_tools::WorldJsonStore::Save(test_world.World());
  EXPECT_TRUE(json.has_value()) << (json ? "" : json.error());
  return z13::testing::WithNormalizedSimulationTick(json.value_or(""));
}

TEST(BrushTest, AClientsPickAndTurnReachTheServerAndOtherClients) {
  const auto network = std::make_shared<z13::net::InMemoryNetwork>();
  Z13TestWorld server(/*skip_main_menu=*/false, {std::string(kServerArg), std::string(kStationArg)}, network);
  Z13TestWorld client_a(
      /*skip_main_menu=*/false, {std::string(kConnectArg), std::string(kTestServerEndpoint)}, network);
  Z13TestWorld client_b(
      /*skip_main_menu=*/false, {std::string(kConnectArg), std::string(kTestServerEndpoint)}, network);
  const auto all = [&](uint64_t ticks) {
    RunNetworkUntil(*network, {server, client_a, client_b}, kNetTestDeltaTime, ticks, [] { return false; });
  };
  ASSERT_TRUE(RunNetworkUntil(*network, {server, client_a, client_b}, kNetTestDeltaTime, kMaxNetTestTicks, [&] {
    return client_a.World().has<z13::gameplay::Gameplay>() && client_b.World().has<z13::gameplay::Gameplay>() &&
        client_a.World().count<z13::gameplay::Player>() == 3 && client_b.World().count<z13::gameplay::Player>() == 3;
  }));

  constexpr uint64_t kSettleTicks = 30;
  const auto step = [&] { all(1); };
  Tap(client_a, Keycode::KEY_TAB, step);
  all(kSettleTicks);
  Tap(client_a, Keycode::KEY_3, step);
  Tap(client_a, Keycode::KEY_R, step);
  all(kSettleTicks);

  const flecs::entity on_server = server.World().lookup(client_a.Player().name().c_str());
  ASSERT_TRUE(on_server.is_valid());
  const BlockSpec expected {
      .type_id = kDoorId, .size = {6, 1, 10}, .orientation = QuarterTurn(Orientation {}, TurnAxis::kZ)};
  EXPECT_EQ(BrushOf(on_server), expected);
  EXPECT_EQ(BrushOf(client_a.Player()), expected);
  EXPECT_EQ(Checkpoint(server), Checkpoint(client_a));
  EXPECT_EQ(Checkpoint(server), Checkpoint(client_b));
}

}  // namespace
}  // namespace z13::building
