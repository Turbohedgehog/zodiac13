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


#include "block_snapshot_system.h"

#include <bit>
#include <cstdint>
#include <memory>
#include <numeric>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

#include <flecs.h>

#include <lib_core/state/snapshot_grouping.h>
#include <lib_core/world/lifecycle.h>

#include <z13/components/station/block.h>
#include <z13/components/station/spawn_point.h>
#include <z13/components/station/wall.h>
#include <primitives/block_group.h>
#include <primitives/placement.h>
#include <z13_settings/building_tuning.h>

#include "block_watch.h"
#include "wall_watch.h"

namespace z13::building {

namespace {

using z13::flecs_tools::MixHash;
using z13::station::Block;
using z13::station::SpawnPoint;

template <typename T>
uint64_t Combine(uint64_t seed, T value) {
  if constexpr (std::is_same_v<T, float>) {
    return MixHash(seed ^ std::bit_cast<uint32_t>(value));
  } else {
    return MixHash(seed ^ static_cast<uint64_t>(value));
  }
}

template <typename T>
uint64_t CombineAll(uint64_t seed, std::span<const T> values) {
  return std::accumulate(
      values.begin(), values.end(), seed, [](uint64_t hash, T value) { return Combine(hash, value); });
}

// The name and every state component a block entity can have (see the test
// BlockSnapshotTest.ABlockHoldsOnlyWhatItsHashCovers).
uint64_t HashOf(flecs::entity e) {
  const Block& block = e.get<Block>();
  const flecs::string_view name = e.name();
  uint64_t hash = MixHash(z13::flecs_tools::HashName(std::string_view(name.c_str(), name.length())));
  hash = Combine(hash, block.spec.type_id);
  hash = Combine(hash, block.grid);
  hash = Combine(hash, std::to_underlying(block.spec.orientation));
  hash = CombineAll(hash, std::span<const int>(block.spec.size.data(), block.spec.size.size()));
  hash = CombineAll(hash, std::span<const int>(block.cell.data(), block.cell.size()));
  if (const auto* spawn_point = e.try_get<SpawnPoint>()) {
    hash = CombineAll(hash, std::span<const float>(spawn_point->transform.data(), spawn_point->transform.size()));
  }
  return hash;
}

// Hashes only the name, StateEntity and one wall component; an entity with other state is encoded every time.
constexpr int kWallEntityTypeSize = 3;

uint64_t HashOfStick(uint64_t hash, const z13::station::WallStick& stick) {
  hash = Combine(hash, std::to_underlying(stick.axis));
  return Combine(hash, stick.length);
}

std::optional<uint64_t> HashOfWallEntity(flecs::entity e) {
  if (e.type().count() != kWallEntityTypeSize) {
    return std::nullopt;
  }
  const flecs::string_view name = e.name();
  const uint64_t seed = MixHash(z13::flecs_tools::HashName(std::string_view(name.c_str(), name.length())));
  if (const auto* node = e.try_get<z13::station::WallNode>()) {
    uint64_t hash = Combine(seed, 1);
    hash = Combine(hash, node->id);
    hash = Combine(hash, node->grid);
    return CombineAll(hash, std::span<const int>(node->cell.data(), node->cell.size()));
  }
  if (const auto* segment = e.try_get<z13::station::WallSegment>()) {
    uint64_t hash = Combine(seed, 2);
    hash = Combine(hash, segment->id);
    hash = Combine(hash, segment->node_a);
    hash = Combine(hash, segment->node_b);
    hash = HashOfStick(hash, segment->stick_a);
    hash = HashOfStick(hash, segment->stick_b);
    hash = Combine(hash, segment->thickness);
    return Combine(hash, std::to_underlying(segment->diagonal));
  }
  if (const auto* opening = e.try_get<z13::station::WallOpening>()) {
    uint64_t hash = Combine(seed, 3);
    hash = Combine(hash, opening->id);
    hash = Combine(hash, opening->segment_id);
    hash = Combine(hash, std::to_underlying(opening->kind));
    hash = Combine(hash, opening->offset);
    hash = Combine(hash, opening->width);
    hash = Combine(hash, opening->sill);
    return Combine(hash, opening->height);
  }
  return std::nullopt;
}

z13::flecs_tools::GroupOf MakeGroupOf(flecs::world world) {
  const int chunk_cells = world.get<z13::BuildingTuning>().index_chunk_cells;
  return [chunk_cells](flecs::entity e) {
    const Block& block = e.get<Block>();
    return z13::building::primitives::BlockGroupId(
        block.grid, z13::building::primitives::ChunkOf(block.cell, chunk_cells));
  };
}

// In the systems stage, not next to the registration (see PhysicsSystem::RegisterSystems).
void InstallGrouping(flecs::world world) {
  const auto watch = std::make_shared<BlockWatch>(world);
  const auto wall_watch = std::make_shared<WallWatch>(world);
  world.set(z13::flecs_tools::SnapshotGrouping {
      .member = world.component<Block>().id(),
      .make_group_of = MakeGroupOf,
      .hash_of = HashOf,
      .unchanged = [watch](flecs::world world) { return watch->Unchanged(world); },
      .hash_ungrouped = HashOfWallEntity,
      .hashed_members = {world.component<z13::station::WallNode>().id(), world.component<z13::station::WallSegment>().id(),
                         world.component<z13::station::WallOpening>().id()},
      .hashed_unchanged = [wall_watch](flecs::world world) { return wall_watch->Unchanged(world); },
  });
}

}  // namespace

void BlockSnapshotSystem::Register(flecs::world& world) {
  OnInitSystems(world, InstallGrouping);
}

}  // namespace z13::building
