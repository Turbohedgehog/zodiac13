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
#include <array>
#include <cstdint>
#include <format>
#include <random>
#include <string>
#include <vector>

#include <Eigen/Dense>

#include <lib_core/state/world_serializer.h>

#include <z13/components/block_change_reader.h>
#include <z13/components/block_changes.h>
#include <z13/components/rooms.h>
#include <z13/components/station.h>
#include <z13_grid/block_index.h>
#include <primitives/placement.h>
#include <rooms/room_builder.h>
#include <rooms/room_cache.h>
#include <z13_settings/building_tuning.h>

#include "../../bullet_module/src/physics_world.h"
#include "../../building_module/tests/support/station_builders.h"
#include "../../z13_module/tests/support/z13_test_world.h"
#include "../src/topology_sources.h"
#include "support/room_blocks.h"

namespace z13::station {
namespace {

using z13::building::grid::BlockIndex;
using z13::building::primitives::CellBox;
using z13::building::primitives::OccupiedCells;
using z13::station::rooms::RoomCache;
using z13::station::rooms::RoomGraph;
using z13::testing::StationWorld;
using z13::testing::Z13TestWorld;

constexpr int kSteps = 80;
constexpr std::array kSeeds {3U, 17U, 2026U};
// Blocks go into a cube of this many cells by kRoomCorner, clear of the site scene.
constexpr int kReach = 24;
constexpr int kLongest = 10;
constexpr int kTallest = 12;
constexpr uint32_t kMostOpsPerTick = 3;

flecs::query<const Block> AllBlocks(Z13TestWorld& world) {
  return world.World().query<const Block>();
}

std::vector<rooms::Room> RoomsByAnchor(const RoomGraph& graph) {
  std::vector<rooms::Room> rooms;
  for (const auto& [id, room] : graph.rooms) {
    rooms.push_back(room);
  }
  std::ranges::sort(rooms, [](const rooms::Room& a, const rooms::Room& b) { return rooms::AnchorBefore(a.anchor, b.anchor); });
  return rooms;
}

// Where what the systems derived from the block changes differs from deriving it from
// every block; empty when nowhere.
std::string Mismatch(Z13TestWorld& world) {
  const flecs::world w = world.World();
  const BlockIndex& index = w.get<BlockIndex>();
  std::string found;
  int blocks {};
  AllBlocks(world).each([&](flecs::entity e, const Block& block) {
    ++blocks;
    if (!std::ranges::contains(index.Overlapping(OccupiedCells(block)), e.id())) {
      found += std::format("index misses {}; ", e.name().c_str());
    }
  });
  if (index.Size() != static_cast<size_t>(blocks)) {
    found += std::format("index holds {} of {} blocks; ", index.Size(), blocks);
  }
  if (w.get<z13::bullet_module::PhysicsWorld>().BodyCount() != static_cast<size_t>(blocks)) {
    found += std::format("{} bodies for {} blocks; ", w.get<z13::bullet_module::PhysicsWorld>().BodyCount(), blocks);
  }

  const TopologySources gathered =
      GatherSources(AllBlocks(world), w.get<z13::building::primitives::BlockPalette>());
  if (w.get<TopologyVersion>().fingerprint != gathered.fingerprint) {
    found += "topology fingerprint; ";
  }
  const auto full = rooms::BuildRooms(gathered.sources, rooms::RoomId {1}, RoomSettingsOf(w.get<z13::BuildingTuning>()));
  const RoomGraph& kept = w.get<RoomCache>().Current()->get();
  if (!full || kept.sources.sealed != full->sources.sealed ||
      kept.sources.portals != full->sources.portals) {
    found += "room sources; ";
  } else if (RoomsByAnchor(kept) != RoomsByAnchor(*full) || kept.portals.size() != full->portals.size()) {
    found += "rooms; ";
  }
  return found;
}

uint32_t Below(std::mt19937& random, uint32_t count) {
  return random() % count;
}

int Between(std::mt19937& random, int low, int high) {
  return low + static_cast<int>(Below(random, static_cast<uint32_t>(high - low + 1)));
}

// A wall, floor, window or cube somewhere in the reach.
Block RandomBlock(std::mt19937& random) {
  constexpr std::array kKinds {testing::kWall, testing::kWall, testing::kFloor, testing::kWindow};
  const Eigen::Vector3i offset(Between(random, 0, kReach), Between(random, 0, kReach), Between(random, 0, kReach));
  const int length = Between(random, 2, kLongest);
  if (Below(random, kKinds.size() + 1) == kKinds.size()) {
    return {.spec = CubeSpec(), .cell = testing::kRoomCorner + offset};
  }
  const std::string_view kind = kKinds[Below(random, kKinds.size())];
  const bool along_y = random() % 2 == 0;
  const Eigen::Vector3i size =
      kind == testing::kFloor ? Eigen::Vector3i(length, Between(random, 2, kLongest), 1)
                              : Eigen::Vector3i(length, 1, Between(random, 2, kTallest));
  return testing::At(kind, size, along_y ? testing::kAlongY : Orientation {}, offset);
}

enum class Op : uint8_t { kBuild, kDestroy, kCapture, kRestore };
// Weights of the ops above, in their order.
constexpr std::array kOpWeights {5U, 3U, 1U, 1U};

Op NextOp(std::mt19937& random) {
  uint32_t roll = Below(random, kOpWeights[0] + kOpWeights[1] + kOpWeights[2] + kOpWeights[3]);
  size_t chosen = 0;
  while (roll >= kOpWeights[chosen]) {
    roll -= kOpWeights[chosen];
    ++chosen;
  }
  return static_cast<Op>(chosen);
}

class BlockChangeTest : public ::testing::TestWithParam<uint32_t> {};

// Builds, destroys and restores of earlier snapshots, several to a tick: every system that
// derives from the blocks ends each tick where deriving from every block does.
TEST_P(BlockChangeTest, DerivedStateFollowsRandomEditsAndRestores) {
  std::mt19937 random(GetParam());
  Z13TestWorld world = StationWorld();
  world.Tick();
  testing::AddBlocks(world, testing::SealedBox());
  world.Tick();
  ASSERT_EQ(Mismatch(world), "");

  std::vector<z13::flecs_tools::WorldSnapshot> snapshots;
  std::vector<flecs::entity> ours;
  int restores {};
  for (int step = 0; step < kSteps; ++step) {
    const uint32_t ops = 1 + Below(random, kMostOpsPerTick);
    for (uint32_t op = 0; op < ops; ++op) {
      std::erase_if(ours, [](flecs::entity e) { return !e.is_alive(); });
      switch (NextOp(random)) {
        case Op::kBuild: {
          const Block block = RandomBlock(random);
          if (!world.World().get<BlockIndex>().Overlaps(OccupiedCells(block))) {
            ours.push_back(testing::AddBlock(world, block));
          }
          break;
        }
        case Op::kDestroy:
          if (!ours.empty()) {
            const flecs::entity doomed = ours[Below(random, static_cast<uint32_t>(ours.size()))];
            DestroyBlock(doomed, doomed.get<Block>());
          }
          break;
        case Op::kCapture: {
          flecs::world w = world.World();
          auto snapshot = z13::flecs_tools::CaptureState(w);
          ASSERT_TRUE(snapshot.has_value());
          snapshots.push_back(std::move(*snapshot));
          break;
        }
        case Op::kRestore:
          if (!snapshots.empty()) {
            flecs::world w = world.World();
            ASSERT_TRUE(z13::flecs_tools::RestoreWorld(w, snapshots[Below(random, static_cast<uint32_t>(snapshots.size()))]));
            ours.clear();
            AllBlocks(world).each([&ours](flecs::entity e, const Block& block) {
              if ((block.cell.array() >= testing::kRoomCorner.array()).all()) {
                ours.push_back(e);
              }
            });
            ++restores;
          }
          break;
      }
    }
    world.Tick();
    ASSERT_EQ(Mismatch(world), "") << "step " << step;
  }
  EXPECT_GT(restores, 0);
}

INSTANTIATE_TEST_SUITE_P(Seeds, BlockChangeTest, ::testing::ValuesIn(kSeeds));

class BlockChangeReaderTest : public ::testing::Test {
 protected:
  // Unread changes cleared, as ClearBlockChangesPhase does on a tick the reader skipped.
  void Clear() { world_.delete_with<BlockChange>(); }

  flecs::world world_;
  BlockChangeReader reader_ {world_};
  const Block wall_ {.spec = CubeSpec()};
};

TEST_F(BlockChangeReaderTest, AMissedTickResyncs) {
  const flecs::entity first = SpawnBlock(world_.entity(), wall_);
  ASSERT_TRUE(reader_.Read(0, 1).resync);
  Clear();
  ASSERT_FALSE(reader_.Read(0, 2).resync);

  // The count is the same, so only the skipped tick tells.
  SpawnBlock(world_.entity(), wall_);
  DestroyBlock(first, wall_);
  Clear();

  EXPECT_TRUE(reader_.Read(0, 4).resync);
}

TEST_F(BlockChangeReaderTest, ABlockBuiltAndDestroyedInOneTickIsNoChange) {
  ASSERT_TRUE(reader_.Read(0, 1).resync);
  const flecs::entity built = SpawnBlock(world_.entity(), wall_);
  DestroyBlock(built, wall_);

  const BlockChangeBatch batch = reader_.Read(0, 2);

  EXPECT_FALSE(batch.resync);
  EXPECT_TRUE(batch.changes.empty());
}

TEST_F(BlockChangeReaderTest, OnlyARestoreThatChangedBlocksResyncs) {
  const flecs::entity block = SpawnBlock(world_.entity(), wall_);
  ASSERT_TRUE(reader_.Read(0, 1).resync);
  Clear();

  EXPECT_FALSE(reader_.Read(1, 2).resync);

  block.set(Block {.spec = CubeSpec(), .cell = Eigen::Vector3i::UnitX()});
  EXPECT_TRUE(reader_.Read(2, 3).resync);
}

// What the systems see comes only through SpawnBlock and DestroyBlock: a block moved past
// them stays where it was for them, and the check above tells.
TEST(BlockChangeTest, ABlockMovedPastTheFunctionsIsCaught) {
  Z13TestWorld world = StationWorld();
  world.Tick();
  testing::AddBlocks(world, testing::SealedBox());
  const flecs::entity wall = testing::AddBlock(world, testing::DoorPartition().back());
  world.Tick();
  ASSERT_EQ(Mismatch(world), "");

  Block moved = wall.get<Block>();
  moved.cell.x() += 2;
  wall.set(moved);
  world.Tick();

  EXPECT_NE(Mismatch(world), "");
}

}  // namespace
}  // namespace z13::station
