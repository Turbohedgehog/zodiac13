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
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

#include <flecs.h>

#include <lib_core/state/snapshot_grouping.h>
#include <lib_core/world/lifecycle.h>

#include <z13/components/station.h>
#include <primitives/placement.h>
#include <z13_settings/building_tuning.h>

#include "block_watch.h"

namespace z13::building {

namespace {

using z13::flecs_tools::MixHash;
using z13::station::Block;
using z13::station::SpawnPoint;

constexpr int kChunkAxisBits = 21;
constexpr int64_t kChunkAxisBias = int64_t {1} << (kChunkAxisBits - 1);
constexpr uint64_t kChunkAxisMask = (uint64_t {1} << kChunkAxisBits) - 1;

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
  hash = Combine(hash, std::to_underlying(block.spec.orientation));
  hash = CombineAll(hash, std::span<const int>(block.spec.size.data(), block.spec.size.size()));
  hash = CombineAll(hash, std::span<const int>(block.cell.data(), block.cell.size()));
  if (const auto* spawn_point = e.try_get<SpawnPoint>()) {
    hash = CombineAll(hash, std::span<const float>(spawn_point->transform.data(), spawn_point->transform.size()));
  }
  return hash;
}

z13::flecs_tools::GroupOf MakeGroupOf(flecs::world world) {
  const int chunk_cells = world.get<z13::BuildingTuning>().index_chunk_cells;
  return [chunk_cells](flecs::entity e) {
    return BlockSnapshotSystem::GroupIdOf(z13::building::primitives::ChunkOf(e.get<Block>().cell, chunk_cells));
  };
}

// In the systems stage, not next to the registration (see PhysicsSystem::RegisterSystems).
void InstallGrouping(flecs::world world) {
  const auto watch = std::make_shared<BlockWatch>(world);
  world.set(z13::flecs_tools::SnapshotGrouping {
      .member = world.component<Block>().id(),
      .make_group_of = [world] { return MakeGroupOf(world); },
      .hash_of = HashOf,
      .unchanged = [watch] { return watch->Unchanged(); },
  });
}

}  // namespace

z13::flecs_tools::SnapshotGroupId BlockSnapshotSystem::GroupIdOf(const Eigen::Vector3i& chunk) {
  return std::accumulate(
      chunk.data(), chunk.data() + chunk.size(), z13::flecs_tools::SnapshotGroupId {},
      [](z13::flecs_tools::SnapshotGroupId id, int axis) {
        return (id << kChunkAxisBits) | (static_cast<uint64_t>(axis + kChunkAxisBias) & kChunkAxisMask);
      });
}

void BlockSnapshotSystem::Register(flecs::world& world) {
  OnInitSystems(world, InstallGrouping);
}

}  // namespace z13::building
