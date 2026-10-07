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

#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <Eigen/Dense>

#include <lib_core/state/rollback.h>
#include <lib_core/state/world_json_store.h>
#include <lib_core/state/world_snapshot_history.h>
#include <lib_core/state/world_state.h>
#include <lib_core/time/simulation_clock.h>
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

void Ticks(Z13TestWorld& test_world, int count) {for (int i = 0; i < count; ++i) {
    test_world.Tick();
  }
}

flecs::entity LocalBuilder(Z13TestWorld& test_world) {test_world.Tick();
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

TEST(BrushTest, ASecondBlockClickedAtTheSameAimRestsAgainstTheFirst) {
  Z13TestWorld test_world = StationWorld();
  const flecs::entity player = LocalBuilder(test_world);
  test_world.World().set(PaletteChoice {.slot = kSlopeSlot});
  Ticks(test_world, 2);

  z13::testing::Click(test_world, Keycode::MOUSE_BUTTON_LEFT);
  test_world.Tick();
  ASSERT_EQ(BlocksOfType(test_world.World(), kSlopeId).size(), 1u);
  const auto preview = PreviewOf(player);
  ASSERT_TRUE(preview.has_value());
  EXPECT_TRUE(preview->valid);

  z13::testing::Click(test_world, Keycode::MOUSE_BUTTON_LEFT);
  test_world.Tick();
  EXPECT_EQ(BlocksOfType(test_world.World(), kSlopeId).size(), 2u);
}

TEST(BrushTest, HoldingThePaletteKeyFreesTheCursorAndClicksBuildNothing) {
  Z13TestWorld test_world = StationWorld();
  LocalBuilder(test_world);
  EXPECT_FALSE(test_world.World().has<z13::gameplay::FreeCursor>());

  test_world.EmitInput(z13::testing::KeyDown(Keycode::KEY_B));
  Ticks(test_world, 2);
  EXPECT_TRUE(test_world.World().has<z13::gameplay::FreeCursor>());
  z13::testing::Click(test_world, Keycode::MOUSE_BUTTON_LEFT);
  test_world.Tick();
  EXPECT_TRUE(BlocksOfType(test_world.World(), kWallId).empty());

  test_world.EmitInput(z13::testing::KeyUp(Keycode::KEY_B));
  Ticks(test_world, 2);
  EXPECT_FALSE(test_world.World().has<z13::gameplay::FreeCursor>());
  z13::testing::Click(test_world, Keycode::MOUSE_BUTTON_LEFT);
  test_world.Tick();
  EXPECT_EQ(BlocksOfType(test_world.World(), kWallId).size(), 1u);
}

// A 24x12 cell wall 16 cells ahead of the player, its thin side facing them, centred on their eye.
struct WallAhead {
  flecs::entity entity;
  Block block;
};

WallAhead AddWallAhead(Z13TestWorld& test_world, flecs::entity player) {const Eigen::Vector3i eye =
      (z13::math::ExtractTranslation<float>(player.get<Eigen::Matrix4f>()) / z13::station::kCellSize)
          .array().floor().cast<int>();
  const Block wall {
      .spec = {.type_id = kWallId, .size = {24, 1, 12}, .orientation = Orientation::kFacePosYUpPosZ},
      .cell = {eye.x() + 16, eye.y() - 12, eye.z() - 6}};
  const flecs::entity entity = test_world.World().entity("TestWall").add<z13::flecs_tools::StateEntity>().set(wall);
  Ticks(test_world, 2);
  return {entity, wall};
}

// The brush turned like the wall, so a door's thin side is along X as well.
void UseDoorBrush(flecs::entity player) {
  player.set(BlockBrush {.spec = {.type_id = kDoorId, .size = {6, 1, 10}, .orientation = Orientation::kFacePosYUpPosZ}});
}

int CellsOf(const std::vector<Block>& blocks) {
  int cells = 0;
  for (const Block& block : blocks) {
    cells += z13::building::primitives::OccupiedCells(block).extent.prod();
  }
  return cells;
}

TEST(BrushTest, HoldingTheCutKeyCutsTheBrushBoxOutOfTheWallInSight) {
  Z13TestWorld test_world = StationWorld();
  const flecs::entity player = LocalBuilder(test_world);
  const WallAhead wall = AddWallAhead(test_world, player);
  UseDoorBrush(player);

  test_world.EmitInput(z13::testing::KeyDown(Keycode::KEY_X));
  Ticks(test_world, 2);
  const auto preview = PreviewOf(player);
  ASSERT_TRUE(preview.has_value());
  EXPECT_EQ(preview->kind, BrushPreview::Kind::kCut);
  EXPECT_TRUE(preview->valid);
  z13::testing::Click(test_world, Keycode::MOUSE_BUTTON_LEFT);
  test_world.Tick();

  EXPECT_FALSE(wall.entity.is_alive());
  const auto pieces = BlocksOfType(test_world.World(), kWallId);
  EXPECT_GE(pieces.size(), 3u);
  EXPECT_EQ(CellsOf(pieces), 24 * 12 - 6 * 10);
  EXPECT_TRUE(BlocksOfType(test_world.World(), kDoorId).empty());
}

TEST(BrushTest, HoldingTheCutInKeyFitsTheBlockIntoTheWall) {
  Z13TestWorld test_world = StationWorld();
  const flecs::entity player = LocalBuilder(test_world);
  const WallAhead wall = AddWallAhead(test_world, player);
  UseDoorBrush(player);

  test_world.EmitInput(z13::testing::KeyDown(Keycode::KEY_LSHIFT));
  Ticks(test_world, 2);
  const auto preview = PreviewOf(player);
  ASSERT_TRUE(preview.has_value());
  EXPECT_EQ(preview->kind, BrushPreview::Kind::kCutIn);
  EXPECT_TRUE(preview->valid);
  z13::testing::Click(test_world, Keycode::MOUSE_BUTTON_LEFT);
  test_world.Tick();

  EXPECT_FALSE(wall.entity.is_alive());
  const auto doors = BlocksOfType(test_world.World(), kDoorId);
  ASSERT_EQ(doors.size(), 1u);
  const auto pieces = BlocksOfType(test_world.World(), kWallId);
  EXPECT_EQ(CellsOf(pieces) + CellsOf(doors), 24 * 12);
  std::vector<Block> all = pieces;
  all.push_back(doors[0]);
  for (size_t i = 0; i < all.size(); ++i) {
    for (size_t j = i + 1; j < all.size(); ++j) {
      EXPECT_FALSE(z13::building::primitives::OccupiedCells(all[i]).Overlaps(
          z13::building::primitives::OccupiedCells(all[j])));
    }
  }
}

TEST(BrushTest, ReleasingTheCutKeyBuildsAgain) {
  Z13TestWorld test_world = StationWorld();
  const flecs::entity player = LocalBuilder(test_world);
  const WallAhead wall = AddWallAhead(test_world, player);
  UseDoorBrush(player);

  test_world.EmitInput(z13::testing::KeyDown(Keycode::KEY_X));
  Ticks(test_world, 2);
  test_world.EmitInput(z13::testing::KeyUp(Keycode::KEY_X));
  Ticks(test_world, 2);
  z13::testing::Click(test_world, Keycode::MOUSE_BUTTON_LEFT);
  test_world.Tick();

  EXPECT_TRUE(wall.entity.is_alive());
  EXPECT_EQ(BlocksOfType(test_world.World(), kDoorId).size(), 1u);
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
  Z13TestWorld server(z13::testing::WithServerArg(z13::testing::SiteArgs()), network);
  Z13TestWorld client_a({std::string(kConnectArg), std::string(kTestServerEndpoint)}, network);
  Z13TestWorld client_b({std::string(kConnectArg), std::string(kTestServerEndpoint)}, network);
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

// A cut makes several blocks from one, named by a counter: every peer has to end with the same ones.
TEST(BrushTest, ACutByAClientLeavesTheSameBlocksOnTheServerAndOtherClients) {
  const auto network = std::make_shared<z13::net::InMemoryNetwork>();
  Z13TestWorld server(z13::testing::WithServerArg(z13::testing::SiteArgs()), network);
  // Before the clients join, so the Welcome snapshot carries it; players spawn about 1.25 m up
  // at the origin, looking along +X.
  server.World().entity("TestWall").add<z13::flecs_tools::StateEntity>().set(Block {
      .spec = {.type_id = kWallId, .size = {24, 1, 12}, .orientation = Orientation::kFacePosYUpPosZ},
      .cell = {16, -12, 0}});
  Z13TestWorld client_a({std::string(kConnectArg), std::string(kTestServerEndpoint)}, network);
  Z13TestWorld client_b({std::string(kConnectArg), std::string(kTestServerEndpoint)}, network);
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
  client_a.EmitInput(z13::testing::KeyDown(Keycode::KEY_X));
  all(kSettleTicks);
  client_a.EmitInput(z13::testing::MouseDown(Keycode::MOUSE_BUTTON_LEFT));
  all(1);
  client_a.EmitInput(z13::testing::MouseUp(Keycode::MOUSE_BUTTON_LEFT));
  all(kSettleTicks);

  EXPECT_GE(BlocksOfType(server.World(), kWallId).size(), 2u);
  EXPECT_EQ(BlocksOfType(client_a.World(), kWallId).size(), BlocksOfType(server.World(), kWallId).size());
  EXPECT_EQ(BlocksOfType(client_b.World(), kWallId).size(), BlocksOfType(server.World(), kWallId).size());
  EXPECT_EQ(Checkpoint(server), Checkpoint(client_a));
  EXPECT_EQ(Checkpoint(server), Checkpoint(client_b));
}

// The cut is replayed from the logged actions, modifier key included.
TEST(BrushTest, ReplayingACutAfterARollbackGivesTheSameState) {
  Z13TestWorld test_world = StationWorld();
  const flecs::entity player = LocalBuilder(test_world);
  AddWallAhead(test_world, player);
  UseDoorBrush(player);

  const float delta_time = 1.f / test_world.Config().GetFPS();
  const uint64_t interval_ticks = static_cast<uint64_t>(
      std::llround(test_world.Config().GetSnapshotIntervalSeconds() * test_world.Config().GetFPS()));
  for (uint64_t i = 0; i < interval_ticks; ++i) {
    test_world.Tick(delta_time);
  }
  const auto& history = test_world.World().get<z13::flecs_tools::WorldSnapshotHistory>().history;
  ASSERT_FALSE(history.Empty());
  const uint64_t rollback_tick = history.Entries().front().tick;

  test_world.EmitInput(z13::testing::KeyDown(Keycode::KEY_X));
  Ticks(test_world, 2);
  z13::testing::Click(test_world, Keycode::MOUSE_BUTTON_LEFT);
  Ticks(test_world, 2);
  test_world.EmitInput(z13::testing::KeyUp(Keycode::KEY_X));
  Ticks(test_world, 2);
  ASSERT_GE(BlocksOfType(test_world.World(), kWallId).size(), 3u);

  const uint64_t target_tick = test_world.World().get<z13::flecs_tools::SimulationClock>().tick;
  const std::string ground_truth = Checkpoint(test_world);
  z13::flecs_tools::RequestRollback(test_world.World(), rollback_tick, target_tick);
  z13::flecs_tools::TickWorld(test_world.World(), delta_time);

  ASSERT_FALSE(test_world.World().has<z13::flecs_tools::RollbackFailed>());
  EXPECT_EQ(Checkpoint(test_world), ground_truth);
}

}  // namespace
}  // namespace z13::building
