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
#include <string>
#include <vector>

#include <Eigen/Dense>

#include <lib_core/state/world_json_store.h>
#include <lib_core/state/world_serializer.h>
#include <lib_core/state/world_state.h>
#include <lib_core/utils/math.h>

#include <net_module/in_memory_transport.h>

#include <z13/components/building.h>
#include <z13/components/gameplay.h>
#include <z13/components/station.h>
#include <z13_module/gameplay/gameplay_entities.h>
#include <z13_primitives/placement.h>

#include "../../z13_module/tests/support/building_test_helpers.h"
#include "../../z13_module/tests/support/test_network.h"
#include "../../z13_module/tests/support/world_json_test_helpers.h"
#include "../../z13_module/tests/support/z13_test_world.h"

// Station-mode building through the real pipeline. Builders are extra players driven by
// their transform and request tags, so they can look anywhere without mouse input.
namespace z13::building {
namespace {

using z13::station::Block;
using z13::station::BlockBrush;
using z13::station::SpawnPoint;
using z13::station::kCellSize;

using z13::testing::kConnectArg;
using z13::testing::kMaxNetTestTicks;
using z13::testing::kNetTestDeltaTime;
using z13::testing::kServerArg;
using z13::testing::kStationArg;
using z13::testing::kTestServerEndpoint;
using z13::testing::RunNetworkUntil;
using z13::testing::Z13TestWorld;
using Keycode = z13::fbs::input::Keycode;

// assets/station/palette.json
constexpr uint32_t kWallId = 2;
constexpr uint32_t kSpawnPointId = 8;
// The default brush (BlockBuildingSystem): a 2 m wall panel.
const Eigen::Vector3i kPanelSize {8, 1, 8};

// Off the cell boundary at y = 0, so the brush and the destroy ray agree on the row.
const Eigen::Vector3f kBuilderPosition {0.f, 0.1f, 1.25f};
constexpr uint32_t kBuilderId = 7;

Z13TestWorld StationWorld() {
  return Z13TestWorld(/*skip_main_menu=*/false, {std::string(kStationArg)});
}

Eigen::Matrix4f Facing(const Eigen::Vector3f& position, const Eigen::Vector3f& forward) {
  Eigen::Matrix4f transform = Eigen::Matrix4f::Identity();
  transform.block<3, 3>(0, 0) =
      Eigen::Quaternionf::FromTwoVectors(Eigen::Vector3f::UnitX(), forward.normalized()).toRotationMatrix();
  z13::math::SetTranslation(position, transform);
  return transform;
}

flecs::entity AddBuilder(Z13TestWorld& test_world, uint32_t id, const Eigen::Matrix4f& transform) {
  flecs::entity builder = z13::gameplay::SpawnPlayer(test_world.World(), id).set(transform);
  builder.add<z13::building::BuildingTool>();
  // The brush and the BlockBrush appear on the first frame.
  test_world.Tick();
  return builder;
}

void Request(Z13TestWorld& test_world, std::vector<flecs::entity> builders, bool build) {
  for (flecs::entity builder : builders) {
    if (build) {
      builder.add<z13::building::RequestBuildBlock>();
    } else {
      builder.add<z13::building::RequestDestroyBlock>();
    }
  }
  test_world.Tick();
}

std::vector<Block> BlocksOfType(flecs::world world, uint32_t type_id) {
  std::vector<Block> blocks;
  world.query_builder<const Block>().build().each([&](const Block& block) {
    if (block.spec.type_id == type_id) {
      blocks.push_back(block);
    }
  });
  return blocks;
}

TEST(BlockBuildingTest, BuildPlacesTheBrushBlockAroundTheBrush) {
  Z13TestWorld test_world = StationWorld();
  const flecs::entity builder = AddBuilder(test_world, kBuilderId, Facing(kBuilderPosition, Eigen::Vector3f::UnitX()));

  Request(test_world, {builder}, /*build=*/true);

  const auto walls = BlocksOfType(test_world.World(), kWallId);
  ASSERT_EQ(walls.size(), 1u);
  EXPECT_EQ(walls[0].spec.size, kPanelSize);
  // The brush sits 5 m ahead of the builder.
  const Eigen::Vector3f brush = (kBuilderPosition + Eigen::Vector3f(5.f, 0.f, 0.f)) / kCellSize;
  EXPECT_TRUE(z13::building::primitives::OccupiedCells(walls[0]).Contains(brush.array().floor().cast<int>()));
}

// Same tick, same cells: the lower player id builds, the other is refused.
TEST(BlockBuildingTest, OverlappingBuildsAreRefused) {
  Z13TestWorld test_world = StationWorld();
  const Eigen::Matrix4f transform = Facing(kBuilderPosition, Eigen::Vector3f::UnitX());
  const flecs::entity first = AddBuilder(test_world, kBuilderId, transform);
  const flecs::entity second = AddBuilder(test_world, kBuilderId + 1, transform);

  Request(test_world, {second, first}, /*build=*/true);
  Request(test_world, {first}, /*build=*/true);

  EXPECT_EQ(BlocksOfType(test_world.World(), kWallId).size(), 1u);
}

TEST(BlockBuildingTest, DestroyRemovesTheWholeBlockInSight) {
  Z13TestWorld test_world = StationWorld();
  const flecs::entity builder = AddBuilder(test_world, kBuilderId, Facing(kBuilderPosition, Eigen::Vector3f::UnitX()));
  Request(test_world, {builder}, /*build=*/true);
  ASSERT_EQ(BlocksOfType(test_world.World(), kWallId).size(), 1u);

  Request(test_world, {builder}, /*build=*/false);

  EXPECT_TRUE(BlocksOfType(test_world.World(), kWallId).empty());
}

// The host steps aside, so only the spawn point's clearance stands in the way.
TEST(BlockBuildingTest, NothingIsBuiltWherePlayersSpawn) {
  Z13TestWorld test_world = StationWorld();
  test_world.Player().set(Facing({0.f, 10.f, 1.25f}, Eigen::Vector3f::UnitX()));
  const Eigen::Vector3f five_meters_before_the_spawn_point = kBuilderPosition - Eigen::Vector3f(5.f, 0.f, 0.f);
  const flecs::entity builder =
      AddBuilder(test_world, kBuilderId, Facing(five_meters_before_the_spawn_point, Eigen::Vector3f::UnitX()));

  Request(test_world, {builder}, /*build=*/true);

  EXPECT_TRUE(BlocksOfType(test_world.World(), kWallId).empty());
  // Players still spawn in the open.
  const Eigen::Matrix4f spawned = z13::gameplay::SpawnPlayer(test_world.World(), kBuilderId + 1).get<Eigen::Matrix4f>();
  EXPECT_NEAR(spawned(2, 3), kBuilderPosition.z(), z13::testing::kTestEpsilon);
}

// The spawn point is built first (lower player id), so the wall later in the same tick
// must already keep clear of it.
TEST(BlockBuildingTest, ASpawnPointBuiltThisTickKeepsItsSpaceClear) {
  Z13TestWorld test_world = StationWorld();
  const flecs::entity marker_builder =
      AddBuilder(test_world, kBuilderId, Facing({0.f, 5.f, 1.25f}, Eigen::Vector3f::UnitX()));
  const flecs::entity wall_builder =
      AddBuilder(test_world, kBuilderId + 1, Facing({0.f, 5.f, 2.5f}, Eigen::Vector3f::UnitX()));
  marker_builder.set(BlockBrush {.spec = {.type_id = kSpawnPointId, .size = {4, 4, 1}}});

  Request(test_world, {wall_builder, marker_builder}, /*build=*/true);

  EXPECT_EQ(BlocksOfType(test_world.World(), kSpawnPointId).size(), 2u);
  EXPECT_TRUE(BlocksOfType(test_world.World(), kWallId).empty());
}

flecs::entity AddSpawnBlock(flecs::world world, const std::string& name, const Eigen::Vector3i& cell) {
  return world.entity(name.c_str())
      .add<z13::flecs_tools::StateEntity>()
      .set(Block {.spec = {.type_id = kSpawnPointId, .size = {4, 4, 1}}, .cell = cell})
      .set(SpawnPoint {});
}

// Builders face +X whatever their transform (the camera follows their absent look input),
// so the spawn point under test stands in front of the builder, the site's own one gone.
TEST(BlockBuildingTest, TheLastSpawnPointCannotBeDestroyed) {
  Z13TestWorld test_world = StationWorld();
  flecs::world world = test_world.World();
  const flecs::entity builder = AddBuilder(test_world, kBuilderId, Facing(kBuilderPosition, Eigen::Vector3f::UnitX()));
  world.lookup("StationSpawnPoint").destruct();
  const Eigen::Vector3i builder_cell = (kBuilderPosition / kCellSize).array().floor().cast<int>();
  const Eigen::Vector3i ahead = builder_cell + Eigen::Vector3i(12, -2, 0);
  const flecs::entity in_sight = AddSpawnBlock(world, "SpawnInSight", ahead);
  test_world.Tick();

  Request(test_world, {builder}, /*build=*/false);
  ASSERT_TRUE(in_sight.is_alive());

  // With another one elsewhere, the one in sight can go.
  AddSpawnBlock(world, "SpawnElsewhere", {40, 40, 0});
  test_world.Tick();
  Request(test_world, {builder}, /*build=*/false);

  EXPECT_FALSE(in_sight.is_alive());
  EXPECT_EQ(world.count<SpawnPoint>(), 1);
}

// The cell index is derived: a restored snapshot brings its blocks' cells back with it.
TEST(BlockBuildingTest, RestoredBlocksTakeTheirCellsAgain) {
  Z13TestWorld test_world = StationWorld();
  const flecs::entity builder = AddBuilder(test_world, kBuilderId, Facing(kBuilderPosition, Eigen::Vector3f::UnitX()));
  Request(test_world, {builder}, /*build=*/true);
  const auto snapshot = z13::flecs_tools::CaptureState(test_world.World());
  ASSERT_TRUE(snapshot.has_value());
  Request(test_world, {builder}, /*build=*/false);
  ASSERT_TRUE(BlocksOfType(test_world.World(), kWallId).empty());

  ASSERT_TRUE(z13::flecs_tools::RestoreWorld(test_world.World(), *snapshot).has_value());
  test_world.Tick();
  const flecs::entity restored_builder = test_world.World().lookup(z13::gameplay::PlayerEntityName(kBuilderId).c_str());
  Request(test_world, {restored_builder}, /*build=*/true);

  EXPECT_EQ(BlocksOfType(test_world.World(), kWallId).size(), 1u);
}

std::string Checkpoint(Z13TestWorld& test_world) {
  const auto json = z13::flecs_tools::WorldJsonStore::Save(test_world.World());
  EXPECT_TRUE(json.has_value()) << (json ? "" : json.error());
  return z13::testing::WithNormalizedSimulationTick(json.value_or(""));
}

// Both joiners share the one spawn point and face the same way, so their walls collide:
// every peer must keep the same one.
TEST(BlockBuildingTest, TwoClientsBuildingTheSameSpotConverge) {
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
  z13::testing::Tap(client_a, Keycode::KEY_TAB, step);
  z13::testing::Tap(client_b, Keycode::KEY_TAB, step);
  all(kSettleTicks);
  client_a.EmitInput(z13::testing::MouseDown(Keycode::MOUSE_BUTTON_LEFT));
  client_b.EmitInput(z13::testing::MouseDown(Keycode::MOUSE_BUTTON_LEFT));
  all(1);
  client_a.EmitInput(z13::testing::MouseUp(Keycode::MOUSE_BUTTON_LEFT));
  client_b.EmitInput(z13::testing::MouseUp(Keycode::MOUSE_BUTTON_LEFT));
  all(kSettleTicks);

  EXPECT_EQ(BlocksOfType(server.World(), kWallId).size(), 1u);
  EXPECT_EQ(Checkpoint(server), Checkpoint(client_a));
  EXPECT_EQ(Checkpoint(server), Checkpoint(client_b));
}

}  // namespace
}  // namespace z13::building
