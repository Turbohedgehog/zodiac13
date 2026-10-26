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
#include <optional>
#include <vector>

#include <Eigen/Dense>

#include <lib_core/state/world_serializer.h>
#include <lib_core/state/world_snapshot_history.h>

#include <net_module/state_digest.h>
#include <sectors/sector_index.h>
#include <z13/components/block.h>
#include <z13_settings/building_tuning.h>
#include <z13_settings/net_tuning.h>

#include "../../../building_module/tests/support/station_builders.h"
#include "../../../station_module/tests/support/room_blocks.h"
#include "../../../z13_module/tests/support/z13_test_world.h"
#include "../../src/sector_hash.h"

namespace z13::net {
namespace {

using z13::testing::StationWorld;
using z13::testing::Z13TestWorld;

constexpr uint32_t kMover = 30;
constexpr float kBuilderHeightMeters = 1.5f;

SectorHashes Hashes(Z13TestWorld& world) {
  flecs::world w = world.World();
  const auto snapshot = z13::flecs_tools::CaptureState(w);
  EXPECT_TRUE(snapshot.has_value());
  return HashBySector(w, *snapshot, w.get<sectors::SectorIndex>(), w.get<z13::BuildingTuning>().index_chunk_cells);
}

int SectorCells(Z13TestWorld& world) {
  return world.World().get<z13::NetTuning>().sector_size_cells;
}

std::optional<uint64_t> HashOf(const SectorHashes& hashes, const sectors::SectorKey& sector) {
  const auto found = std::ranges::find_if(hashes.by_sector, [&](const fbs::net::SectorHashWire& entry) {
    return entry.x() == sector.x() && entry.y() == sector.y() && entry.z() == sector.z();
  });
  return found == hashes.by_sector.end() ? std::nullopt : std::optional(found->hash());
}

TEST(SectorHashTest, ABlockChangesOnlyItsOwnSectorsHash) {
  Z13TestWorld world = StationWorld();
  world.Tick();
  const SectorHashes before = Hashes(world);
  const int sector_cells = SectorCells(world);
  const Eigen::Vector3i cell = Eigen::Vector3i::Constant(5 * sector_cells + 1);

  z13::station::testing::AddBlock(world, {.spec = z13::station::CubeSpec(), .cell = cell});
  world.Tick();
  const SectorHashes after = Hashes(world);

  const sectors::SectorKey added = sectors::SectorOf(cell, sector_cells);
  EXPECT_FALSE(HashOf(before, added).has_value());
  EXPECT_TRUE(HashOf(after, added).has_value());
  for (const fbs::net::SectorHashWire& entry : before.by_sector) {
    EXPECT_EQ(HashOf(after, {entry.x(), entry.y(), entry.z()}), entry.hash());
  }
  EXPECT_EQ(after.by_sector.size(), before.by_sector.size() + 1);
}

TEST(SectorHashTest, APlayerMovesItsHashToTheSectorItEntered) {
  Z13TestWorld world = StationWorld();
  const int sector_cells = SectorCells(world);
  const float sector_meters = static_cast<float>(sector_cells) * z13::station::kCellSize;
  const flecs::entity mover = z13::testing::AddBuilder(
      world, kMover, z13::testing::Facing({7.5f * sector_meters, 0.5f * sector_meters, kBuilderHeightMeters}, Eigen::Vector3f::UnitX()));
  world.Tick();
  const SectorHashes there = Hashes(world);

  mover.set(z13::testing::Facing({9.5f * sector_meters, 0.5f * sector_meters, kBuilderHeightMeters}, Eigen::Vector3f::UnitX()));
  world.Tick();
  const SectorHashes moved = Hashes(world);

  EXPECT_TRUE(HashOf(there, {7, 0, 0}).has_value());
  EXPECT_FALSE(HashOf(moved, {7, 0, 0}).has_value());
  EXPECT_TRUE(HashOf(moved, {9, 0, 0}).has_value());
}

TEST(SectorHashTest, ADigestCarriesTheSectorHashesOfItsSnapshot) {
  Z13TestWorld world = StationWorld();
  world.Tick();
  flecs::world w = world.World();
  const auto snapshot = z13::flecs_tools::CaptureState(w);
  ASSERT_TRUE(snapshot.has_value());

  const fbs::net::StateDigestT digest = ComputeStateDigest(w, {.tick = 1, .snapshot = *snapshot});
  const SectorHashes expected = Hashes(world);

  EXPECT_EQ(digest.global_hash, expected.global);
  ASSERT_EQ(digest.sector_hashes.size(), expected.by_sector.size());
  EXPECT_GT(digest.sector_hashes.size(), 0u);
}

}  // namespace
}  // namespace z13::net
