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
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Eigen/Dense>

#include <lib_core/state/snapshot_grouping.h>
#include <lib_core/state/world_serializer.h>
#include <lib_core/state/world_state.h>

#include <z13/components/station.h>

#include "../../z13_module/tests/support/z13_test_world.h"
#include "support/station_builders.h"

// Blocks are snapshotted by index chunk, and a chunk that didn't change is shared with
// the previous snapshot.
namespace z13::building {
namespace {

namespace ft = z13::flecs_tools;
using z13::station::Block;
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

// What a snapshot of a block holds must be what its group hash covers.
TEST(BlockSnapshotTest, ABlockIsNothingButItsBlockComponent) {
  Z13TestWorld test_world = StationWorld();
  const flecs::entity block = PlaceBlock(test_world.World(), "TestBlock", kFirstChunkCell);

  const auto snapshot = ft::CaptureEntityState(test_world.World(), block);

  ASSERT_TRUE(snapshot.has_value());
  ASSERT_EQ(snapshot->entities.size(), 1u);
  EXPECT_EQ(snapshot->entities[0].components.size(), 1u);
  EXPECT_TRUE(snapshot->entities[0].tags.empty());
  EXPECT_TRUE(snapshot->entities[0].relationships.empty());
}

TEST(BlockSnapshotTest, GroupingDoesNotChangeWhatTheSnapshotHolds) {
  Z13TestWorld test_world = StationWorld();
  flecs::world world = test_world.World();
  PlaceBlock(world, "TestBlock_0", kFirstChunkCell);
  PlaceBlock(world, "TestBlock_1", kSecondChunkCell);
  PlaceBlock(world, "TestBlock_2", kThirdChunkCell);

  const ft::WorldSnapshot grouped = ft::CaptureState(world).value();
  const auto group_of = std::exchange(world.get_mut<ft::SnapshotGrouping>().group_of, nullptr);
  const ft::WorldSnapshot ungrouped = ft::CaptureState(world).value();
  world.get_mut<ft::SnapshotGrouping>().group_of = group_of;

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

// A cached chunk must follow every kind of edit: a block moved, added and destroyed.
TEST(BlockSnapshotTest, ACachedChunkNeverGoesStale) {
  Z13TestWorld test_world = StationWorld();
  flecs::world world = test_world.World();
  const flecs::entity moving = PlaceBlock(world, "TestBlock_0", kFirstChunkCell);
  const flecs::entity doomed = PlaceBlock(world, "TestBlock_1", kSecondChunkCell);
  ft::CaptureState(world).value();

  const auto matches_ungrouped = [&world] {
    const StateByName grouped = Flatten(ft::CaptureState(world).value());
    const auto group_of = std::exchange(world.get_mut<ft::SnapshotGrouping>().group_of, nullptr);
    const StateByName ungrouped = Flatten(ft::CaptureState(world).value());
    world.get_mut<ft::SnapshotGrouping>().group_of = group_of;
    return grouped == ungrouped;
  };

  moving.set(Block {.spec = z13::station::CubeSpec(), .cell = kNextToFirst});
  EXPECT_TRUE(matches_ungrouped());
  PlaceBlock(world, "TestBlock_2", kThirdChunkCell);
  EXPECT_TRUE(matches_ungrouped());
  doomed.destruct();
  EXPECT_TRUE(matches_ungrouped());
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

}  // namespace
}  // namespace z13::building
