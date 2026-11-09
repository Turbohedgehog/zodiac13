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
#include <cstdint>
#include <format>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Eigen/Dense>

#include <lib_core/state/snapshot_grouping.h>
#include <lib_core/state/world_serializer.h>
#include <lib_core/state/state_markers.h>

#include <z13/components/station/block.h>
#include <z13/components/station/block_changes.h>
#include <z13/components/gameplay/gameplay.h>
#include <z13/components/station/grid.h>
#include <z13/components/station/spawn_point.h>
#include <z13/components/station/wall.h>
#include <grid/block_index.h>
#include <primitives/palette.h>
#include <z13_settings/building_tuning.h>

#include "../../z13_module/tests/support/z13_test_world.h"
#include "../src/block_entities.h"
#include "support/station_builders.h"

namespace z13::building {
namespace {

namespace ft = z13::flecs_tools;
using z13::building::grid::BlockIndex;
using z13::station::Block;
using z13::station::Grid;
using z13::station::SpawnPoint;
using z13::testing::StationWorld;
using z13::testing::Z13TestWorld;

// Far from anything the site scene builds, one chunk apart (16 cells each).
const Eigen::Vector3i kFirstChunkCell {1000, 0, 0};
const Eigen::Vector3i kNextToFirst {1001, 0, 0};
const Eigen::Vector3i kSecondChunkCell {1100, 0, 0};
const Eigen::Vector3i kThirdChunkCell {1200, 0, 0};
const Eigen::Vector3i kMovedSecond {1101, 0, 0};

using StateByName = std::map<std::string, std::vector<std::pair<std::string, std::vector<uint8_t>>>>;

StateByName Flatten(const ft::WorldSnapshot& snapshot) {
  StateByName state;
  for (const ft::EntitySnapshot& entity : ft::AllEntities(snapshot)) {
    auto& components = state[entity.name];
    for (const ft::ComponentValue& component : entity.components) {
      components.emplace_back(component.type, component.value);
    }
  }
  return state;
}

flecs::entity PlaceBlock(flecs::world world, std::string_view name, const Eigen::Vector3i& cell) {
  return world.entity(std::string(name).c_str())
      .add<ft::StateEntity>()
      .set(Block {.spec = z13::station::CubeSpec(), .cell = cell});
}

// The group hash (HashOf) covers Block and SpawnPoint; a block of any primitive holds nothing else.
TEST(BlockSnapshotTest, ABlockHoldsOnlyWhatItsHashCovers) {
  Z13TestWorld test_world = StationWorld();
  flecs::world world = test_world.World();
  const auto& palette = world.get<z13::building::primitives::BlockPalette>().palette;
  const auto& tuning = world.get<z13::BuildingTuning>();
  const std::set<flecs::entity_t> covered {world.component<Block>().id(), world.component<SpawnPoint>().id()};
  int spawn_points = 0;

  for (const auto& primitive : palette.primitives) {
    const Block block {.spec = {.type_id = primitive.id, .size = primitive.min_size}, .cell = kFirstChunkCell};
    const flecs::entity e = CreateBlock(world, std::format("TestBlock_{}", primitive.name), block, palette, tuning);
    spawn_points += e.has<SpawnPoint>() ? 1 : 0;

    const auto snapshot = ft::CaptureEntityState(world, e);

    ASSERT_TRUE(snapshot.has_value()) << snapshot.error();
    ASSERT_EQ(snapshot->entities.size(), 1u);
    for (const ft::ComponentValue& component : snapshot->entities[0]->components) {
      EXPECT_TRUE(covered.contains(world.lookup(component.type.c_str()).id())) << primitive.name << ": " << component.type;
    }
    EXPECT_TRUE(snapshot->entities[0]->tags.empty()) << primitive.name;
    EXPECT_TRUE(snapshot->entities[0]->relationships.empty()) << primitive.name;
    e.destruct();
  }
  EXPECT_GT(spawn_points, 0);
}

TEST(BlockSnapshotTest, GroupingDoesNotChangeWhatTheSnapshotHolds) {
  Z13TestWorld test_world = StationWorld();
  flecs::world world = test_world.World();
  PlaceBlock(world, "TestBlock_0", kFirstChunkCell);
  PlaceBlock(world, "TestBlock_1", kSecondChunkCell);
  PlaceBlock(world, "TestBlock_2", kThirdChunkCell);

  const ft::WorldSnapshot grouped = ft::CaptureState(world).value();
  const auto make_group_of = std::exchange(world.get_mut<ft::SnapshotGrouping>().make_group_of, nullptr);
  const ft::WorldSnapshot ungrouped = ft::CaptureState(world).value();
  world.get_mut<ft::SnapshotGrouping>().make_group_of = make_group_of;

  EXPECT_FALSE(grouped.groups.empty());
  EXPECT_TRUE(ungrouped.groups.empty());
  EXPECT_EQ(Flatten(grouped), Flatten(ungrouped));
}

TEST(BlockSnapshotTest, OnlyTheChunkThatChangedIsEncodedAgain) {
  Z13TestWorld test_world = StationWorld();
  flecs::world world = test_world.World();
  PlaceBlock(world, "TestBlock_0", kFirstChunkCell);
  PlaceBlock(world, "TestBlock_1", kSecondChunkCell);
  PlaceBlock(world, "TestBlock_2", kThirdChunkCell);
  const ft::WorldSnapshot before = ft::CaptureState(world).value();

  PlaceBlock(world, "TestBlock_3", kNextToFirst);
  const ft::WorldSnapshot after = ft::CaptureState(world).value();

  ASSERT_EQ(after.groups.size(), before.groups.size());
  const auto shared = std::ranges::count_if(after.groups, [&before](const auto& group) {
    return std::ranges::find(before.groups, group) != before.groups.end();
  });
  EXPECT_EQ(static_cast<size_t>(shared), before.groups.size() - 1);
}

TEST(BlockSnapshotTest, NothingChangedSharesEveryChunk) {
  Z13TestWorld test_world = StationWorld();
  flecs::world world = test_world.World();
  PlaceBlock(world, "TestBlock_0", kFirstChunkCell);
  const ft::WorldSnapshot before = ft::CaptureState(world).value();

  const ft::WorldSnapshot after = ft::CaptureState(world).value();

  EXPECT_FALSE(before.groups.empty());
  EXPECT_EQ(after.groups, before.groups);
}

TEST(BlockSnapshotTest, ACachedChunkNeverGoesStale) {
  Z13TestWorld test_world = StationWorld();
  flecs::world world = test_world.World();
  const flecs::entity moving = PlaceBlock(world, "TestBlock_0", kFirstChunkCell);
  const flecs::entity doomed = PlaceBlock(world, "TestBlock_1", kSecondChunkCell);
  ft::CaptureState(world).value();

  const auto matches_ungrouped = [&world] {
    const StateByName grouped = Flatten(ft::CaptureState(world).value());
    const auto make_group_of = std::exchange(world.get_mut<ft::SnapshotGrouping>().make_group_of, nullptr);
    const StateByName ungrouped = Flatten(ft::CaptureState(world).value());
    world.get_mut<ft::SnapshotGrouping>().make_group_of = make_group_of;
    return grouped == ungrouped;
  };

  moving.set(Block {.spec = z13::station::CubeSpec(), .cell = kNextToFirst});
  EXPECT_TRUE(matches_ungrouped());
  moving.set(Block {.spec = z13::station::CubeSpec(), .cell = kNextToFirst, .grid = 1});
  EXPECT_TRUE(matches_ungrouped());
  moving.set(SpawnPoint {});
  EXPECT_TRUE(matches_ungrouped());
  moving.set(SpawnPoint {.transform = Eigen::Matrix4f::Constant(1.f)});
  EXPECT_TRUE(matches_ungrouped());
  PlaceBlock(world, "TestBlock_2", kThirdChunkCell);
  EXPECT_TRUE(matches_ungrouped());
  doomed.destruct();
  EXPECT_TRUE(matches_ungrouped());
}

flecs::entity PlaceWallEntity(flecs::world world, std::string_view name) {
  return world.entity(std::string(name).c_str()).add<ft::StateEntity>();
}

// The wall hash (HashOfWallEntity) covers the wall components; an entity holding anything else is not cached.
TEST(BlockSnapshotTest, AWallEntityHoldsOnlyWhatItsHashCovers) {
  Z13TestWorld test_world = StationWorld();
  flecs::world world = test_world.World();
  const flecs::entity node = PlaceWallEntity(world, "TestWallNode").set(z13::station::WallNode {.id = 1});
  const flecs::entity segment = PlaceWallEntity(world, "TestWallSegment").set(z13::station::WallSegment {.id = 2});
  const flecs::entity opening = PlaceWallEntity(world, "TestWallOpening").set(z13::station::WallOpening {.id = 3});

  for (const flecs::entity e : {node, segment, opening}) {
    const auto snapshot = ft::CaptureEntityState(world, e);
    ASSERT_TRUE(snapshot.has_value()) << snapshot.error();
    ASSERT_EQ(snapshot->entities.size(), 1u);
    EXPECT_EQ(snapshot->entities[0]->components.size(), 1u) << e.name();
    EXPECT_TRUE(snapshot->entities[0]->tags.empty()) << e.name();
    EXPECT_TRUE(snapshot->entities[0]->relationships.empty()) << e.name();
    EXPECT_TRUE(world.get<ft::SnapshotGrouping>().hash_ungrouped(e).has_value()) << e.name();
  }
}

TEST(BlockSnapshotTest, ACachedWallNeverGoesStale) {
  Z13TestWorld test_world = StationWorld();
  flecs::world world = test_world.World();
  const flecs::entity node = PlaceWallEntity(world, "TestWallNode").set(z13::station::WallNode {.id = 1});
  const flecs::entity segment = PlaceWallEntity(world, "TestWallSegment").set(z13::station::WallSegment {.id = 2});
  const flecs::entity opening = PlaceWallEntity(world, "TestWallOpening").set(z13::station::WallOpening {.id = 3});
  ft::CaptureState(world).value();

  const auto matches_uncached = [&world] {
    const StateByName cached = Flatten(ft::CaptureState(world).value());
    const auto hash_ungrouped = std::exchange(world.get_mut<ft::SnapshotGrouping>().hash_ungrouped, nullptr);
    const StateByName uncached = Flatten(ft::CaptureState(world).value());
    world.get_mut<ft::SnapshotGrouping>().hash_ungrouped = hash_ungrouped;
    return cached == uncached;
  };

  EXPECT_TRUE(matches_uncached());
  node.set(z13::station::WallNode {.id = 1, .cell = Eigen::Vector3i(1, 0, 0)});
  EXPECT_TRUE(matches_uncached());
  node.set(z13::station::WallNode {.id = 1, .grid = 1, .cell = Eigen::Vector3i(1, 0, 0)});
  EXPECT_TRUE(matches_uncached());
  segment.set(z13::station::WallSegment {.id = 2, .stick_a = {.length = 3}});
  EXPECT_TRUE(matches_uncached());
  segment.set(z13::station::WallSegment {.id = 2, .stick_a = {.length = 3}, .thickness = -1});
  EXPECT_TRUE(matches_uncached());
  opening.set(z13::station::WallOpening {.id = 3, .width = 2});
  EXPECT_TRUE(matches_uncached());
  opening.set(z13::station::WallOpening {.id = 3, .kind = z13::station::OpeningKind::kWindow, .width = 2});
  EXPECT_TRUE(matches_uncached());
  PlaceWallEntity(world, "TestWallNode_2").set(z13::station::WallNode {.id = 4});
  EXPECT_TRUE(matches_uncached());
  node.destruct();
  EXPECT_TRUE(matches_uncached());
}

// Counts the fingerprints given while `capture` runs (an entity that can't be fingerprinted gets none).
int HashesTakenBy(flecs::world world, const std::function<void()>& capture) {
  int taken = 0;
  auto& grouping = world.get_mut<ft::SnapshotGrouping>();
  const auto hash_ungrouped = grouping.hash_ungrouped;
  grouping.hash_ungrouped = [&taken, &hash_ungrouped](flecs::entity e) {
    const auto hash = hash_ungrouped(e);
    taken += hash.has_value() ? 1 : 0;
    return hash;
  };
  capture();
  world.get_mut<ft::SnapshotGrouping>().hash_ungrouped = hash_ungrouped;
  return taken;
}

TEST(BlockSnapshotTest, QuietWallsAreNotVisitedAgain) {
  Z13TestWorld test_world = StationWorld();
  flecs::world world = test_world.World();
  const flecs::entity node = PlaceWallEntity(world, "TestWallNode").set(z13::station::WallNode {.id = 1});
  PlaceWallEntity(world, "TestWallSegment").set(z13::station::WallSegment {.id = 2});
  const StateByName first = Flatten(ft::CaptureState(world).value());
  ft::CaptureState(world).value();

  StateByName quiet;
  EXPECT_EQ(HashesTakenBy(world, [&] { quiet = Flatten(ft::CaptureState(world).value()); }), 0);
  EXPECT_EQ(quiet, first);

  node.set(z13::station::WallNode {.id = 1, .cell = Eigen::Vector3i(1, 0, 0)});
  StateByName moved;
  EXPECT_GT(HashesTakenBy(world, [&] { moved = Flatten(ft::CaptureState(world).value()); }), 0);
  EXPECT_NE(moved, first);
}

TEST(BlockSnapshotTest, AnUnchangedWallIsTheSameRecordInTheNextSnapshot) {
  Z13TestWorld test_world = StationWorld();
  flecs::world world = test_world.World();
  const flecs::entity node = PlaceWallEntity(world, "TestWallNode").set(z13::station::WallNode {.id = 1});
  PlaceWallEntity(world, "TestWallSegment").set(z13::station::WallSegment {.id = 2});
  const auto find = [](const ft::WorldSnapshot& snapshot, std::string_view name) {
    const auto found = std::ranges::find_if(snapshot.entities, [name](const auto& e) { return e->name == name; });
    return found == snapshot.entities.end() ? ft::SharedEntity {} : *found;
  };
  ft::CaptureState(world).value();
  const ft::WorldSnapshot before = ft::CaptureState(world).value();

  node.set(z13::station::WallNode {.id = 1, .cell = Eigen::Vector3i(1, 0, 0)});
  const ft::WorldSnapshot after = ft::CaptureState(world).value();

  ASSERT_TRUE(find(before, "TestWallSegment"));
  EXPECT_EQ(find(after, "TestWallSegment"), find(before, "TestWallSegment"));
  EXPECT_NE(find(after, "TestWallNode"), find(before, "TestWallNode"));
}

TEST(BlockSnapshotTest, AWallGainingOtherStateIsEncodedAgain) {
  Z13TestWorld test_world = StationWorld();
  flecs::world world = test_world.World();
  const flecs::entity node = PlaceWallEntity(world, "TestWallNode").set(z13::station::WallNode {.id = 1});
  ft::CaptureState(world).value();
  ft::CaptureState(world).value();

  node.set(z13::station::WallPanelOf {.segment_id = 7});
  const auto with_panel = Flatten(ft::CaptureState(world).value());
  EXPECT_EQ(with_panel.at("TestWallNode").size(), 2u);
  EXPECT_EQ(Flatten(ft::CaptureState(world).value()), with_panel);
}

TEST(BlockSnapshotTest, RestoringWallsDropsWhatWasCached) {
  Z13TestWorld test_world = StationWorld();
  flecs::world world = test_world.World();
  const flecs::entity node = PlaceWallEntity(world, "TestWallNode").set(z13::station::WallNode {.id = 1});
  const ft::WorldSnapshot before = ft::CaptureState(world).value();
  const StateByName expected = Flatten(before);
  ft::CaptureState(world).value();

  node.set(z13::station::WallNode {.id = 1, .cell = Eigen::Vector3i(5, 0, 0)});
  ft::CaptureState(world).value();
  ASSERT_TRUE(ft::RestoreWorld(world, before).has_value());
  EXPECT_EQ(Flatten(ft::CaptureState(world).value()), expected);
}

TEST(BlockSnapshotTest, RestoreTouchesOnlyTheChunksThatChanged) {
  Z13TestWorld test_world = StationWorld();
  flecs::world world = test_world.World();
  PlaceBlock(world, "TestBlock_0", kFirstChunkCell);
  const flecs::entity moving = PlaceBlock(world, "TestBlock_1", kSecondChunkCell);
  PlaceBlock(world, "TestBlock_2", kThirdChunkCell);
  const ft::WorldSnapshot snapshot = ft::CaptureState(world).value();
  moving.set(Block {.spec = z13::station::CubeSpec(), .cell = kMovedSecond});

  std::map<std::string, int> sets;
  world.observer<const Block>().event(flecs::OnSet).each([&sets](flecs::entity e, const Block&) {
    ++sets[e.name().c_str()];
  });
  ASSERT_TRUE(ft::RestoreWorld(world, snapshot).has_value());

  EXPECT_EQ(sets["TestBlock_0"], 0);
  EXPECT_EQ(sets["TestBlock_1"], 1);
  EXPECT_EQ(sets["TestBlock_2"], 0);
  EXPECT_EQ(moving.get<Block>().cell, kSecondChunkCell);
}

TEST(GridModelTest, TheMainGridStandsWhereTheStationIs) {
  Z13TestWorld test_world = StationWorld();
  flecs::world world = test_world.World();

  const flecs::entity main_grid = world.lookup(std::string(z13::station::kMainGridName).c_str());

  ASSERT_TRUE(main_grid.is_valid());
  EXPECT_TRUE(main_grid.has<ft::StateEntity>());
  EXPECT_EQ(main_grid.get<Grid>(), Grid {});
  EXPECT_EQ(PlaceBlock(world, "TestBlock_0", kFirstChunkCell).get<Block>().grid, z13::station::kMainGrid);
}

TEST(GridModelTest, TheGridOfABlockAndThePoseOfAGridSurviveARestore) {
  Z13TestWorld test_world = StationWorld();
  flecs::world world = test_world.World();
  const Grid pose {.id = 1, .origin = {40, -8, 12}, .yaw_degrees = 50.f};
  const flecs::entity second = world.entity("SecondGrid").add<ft::StateEntity>().set(pose);
  const flecs::entity block = PlaceBlock(world, "TestBlock_0", kFirstChunkCell);
  block.set(Block {.spec = z13::station::CubeSpec(), .cell = kFirstChunkCell, .grid = pose.id});
  const ft::WorldSnapshot snapshot = ft::CaptureState(world).value();
  block.set(Block {.spec = z13::station::CubeSpec(), .cell = kFirstChunkCell});
  second.set(Grid {});

  ASSERT_TRUE(ft::RestoreWorld(world, snapshot).has_value());

  EXPECT_EQ(block.get<Block>().grid, pose.id);
  EXPECT_EQ(world.lookup("SecondGrid").get<Grid>(), pose);
}


TEST(GridModelTest, TheMainGridIsMadeAgainWithEverySceneNotOnlyTheFirst) {
  Z13TestWorld test_world = StationWorld();
  flecs::world world = test_world.World();
  test_world.Tick();
  ASSERT_TRUE(z13::station::MainGridEntity(world).is_valid());

  world.remove<z13::gameplay::Gameplay>();
  EXPECT_FALSE(z13::station::MainGridEntity(world).is_valid()) << "leaving the station destroys its state";
  world.add<z13::gameplay::Gameplay>();

  const flecs::entity again = z13::station::MainGridEntity(world);
  ASSERT_TRUE(again.is_valid());
  EXPECT_TRUE(again.has<ft::StateEntity>());
  EXPECT_TRUE(again.has<BlockIndex>());
}

// What is derived from a grid's blocks is a component of its entity.
class GridIndexTest : public ::testing::Test {
 protected:
  void SetUp() override {
    world_.Tick();
    second_ = world_.World().entity("SecondGrid").add<ft::StateEntity>().set(Grid {.id = kSecondGrid});
    world_.Tick();
  }

  flecs::entity PlaceInSecondGrid() {
    const flecs::entity block = z13::station::SpawnBlock(
        world_.World().entity("SecondGridBlock"), Block {.spec = z13::station::CubeSpec(), .cell = kFirstChunkCell, .grid = kSecondGrid});
    world_.Tick();
    return block;
  }

  size_t MainIndexSize() {
    return z13::station::MainGridEntity(world_.World()).get<BlockIndex>().Size();
  }

  static constexpr z13::station::GridId kSecondGrid = 1;
  Z13TestWorld world_ = StationWorld();
  flecs::entity second_;
};

TEST_F(GridIndexTest, ABlockOfAnotherGridIsIndexedThereAndNotInTheMainGrid) {
  const size_t main_before = MainIndexSize();

  const flecs::entity block = PlaceInSecondGrid();

  EXPECT_EQ(MainIndexSize(), main_before);
  EXPECT_FALSE(z13::station::MainGridEntity(world_.World()).get<BlockIndex>().At(kFirstChunkCell).has_value());
  ASSERT_TRUE(second_.has<BlockIndex>());
  EXPECT_EQ(second_.get<BlockIndex>().Size(), 1u);
  EXPECT_EQ(second_.get<BlockIndex>().At(kFirstChunkCell), std::optional<z13::building::grid::BlockId>(block.id()));
}

TEST_F(GridIndexTest, DestroyingItEmptiesOnlyItsGridsIndex) {
  const size_t main_before = MainIndexSize();
  const flecs::entity block = PlaceInSecondGrid();

  z13::station::DestroyBlock(block, block.get<Block>());
  world_.Tick();

  EXPECT_EQ(second_.get<BlockIndex>().Size(), 0u);
  EXPECT_EQ(MainIndexSize(), main_before);
}

TEST_F(GridIndexTest, ARestoreIndexesEveryGridAgain) {
  PlaceInSecondGrid();
  const size_t main_size = MainIndexSize();
  const ft::WorldSnapshot snapshot = ft::CaptureState(world_.World()).value();
  z13::station::SpawnBlock(world_.World().entity("Extra"), Block {.spec = z13::station::CubeSpec(), .cell = kSecondChunkCell});
  world_.Tick();

  ASSERT_TRUE(ft::RestoreWorld(world_.World(), snapshot).has_value());
  world_.Tick();

  EXPECT_EQ(MainIndexSize(), main_size);
  ASSERT_TRUE(world_.World().lookup("SecondGrid").has<BlockIndex>());
  EXPECT_EQ(world_.World().lookup("SecondGrid").get<BlockIndex>().Size(), 1u);
}

}  // namespace
}  // namespace z13::building
