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

#include <cstdint>
#include <functional>
#include <memory>
#include <string_view>

#include <flecs.h>

#include <lib_core/state/snapshot_grouping.h>
#include <lib_core/world/lifecycle.h>

#include <z13/components/station.h>
#include <z13_primitives/placement.h>
#include <z13_settings/building_tuning.h>

namespace z13::building {

namespace {

constexpr int kChunkAxisBits = 21;
constexpr int64_t kChunkAxisBias = int64_t {1} << (kChunkAxisBits - 1);
constexpr uint64_t kChunkAxisMask = (uint64_t {1} << kChunkAxisBits) - 1;

std::optional<z13::flecs_tools::SnapshotGroupId> GroupOf(flecs::world world, flecs::entity e) {
  const auto* block = e.try_get<z13::station::Block>();
  if (block == nullptr) {
    return std::nullopt;
  }
  const int chunk_cells = world.get<z13::BuildingTuning>().index_chunk_cells;
  return BlockSnapshotSystem::GroupIdOf(z13::building::primitives::ChunkOf(block->cell, chunk_cells));
}

uint64_t Combine(uint64_t seed, int64_t value) {
  return z13::flecs_tools::MixHash(seed ^ static_cast<uint64_t>(value));
}

// Name and every field of Block: what a snapshot of the entity holds.
uint64_t HashOf(flecs::entity e) {
  const auto& block = e.get<z13::station::Block>();
  const flecs::string_view name = e.name();
  uint64_t hash = z13::flecs_tools::MixHash(std::hash<std::string_view> {}(std::string_view(name.c_str(), name.length())));
  hash = Combine(hash, block.spec.type_id);
  hash = Combine(hash, static_cast<int64_t>(block.spec.orientation));
  for (int axis = 0; axis < 3; ++axis) {
    hash = Combine(hash, block.spec.size[axis]);
    hash = Combine(hash, block.cell[axis]);
  }
  return hash;
}

// The blocks as they were when `unchanged` last asked.
struct BlockWatch {
  flecs::query<const z13::station::Block> blocks;
  bool seen {};
  int count {};
  int chunk_cells {};
};

bool BlocksUnchanged(flecs::world world, BlockWatch& watch) {
  // Checked before the walk: iterating the query resets its changed state (count() doesn't).
  const bool changed = watch.blocks.changed();
  int count = 0;
  watch.blocks.run([&count](flecs::iter& it) {
    while (it.next()) {
      count += static_cast<int>(it.count());
    }
  });
  const int chunk_cells = world.get<z13::BuildingTuning>().index_chunk_cells;
  const bool unchanged = watch.seen && !changed && count == watch.count && chunk_cells == watch.chunk_cells;
  watch.seen = true;
  watch.count = count;
  watch.chunk_cells = chunk_cells;
  return unchanged;
}

// In the systems stage, not next to the registration (see PhysicsSystem::RegisterSystems).
void InstallGrouping(flecs::world world) {
  const auto watch = std::make_shared<BlockWatch>(BlockWatch {
      .blocks = world.query_builder<const z13::station::Block>("BlockSnapshotSystem::BlockQuery")
                    .detect_changes()
                    .build()});
  world.set(z13::flecs_tools::SnapshotGrouping {
      .member = world.component<z13::station::Block>().id(),
      .group_of = [world](flecs::entity e) { return GroupOf(world, e); },
      .hash_of = HashOf,
      .unchanged = [world, watch] { return BlocksUnchanged(world, *watch); },
  });
}

}  // namespace

z13::flecs_tools::SnapshotGroupId BlockSnapshotSystem::GroupIdOf(const Eigen::Vector3i& chunk) {
  z13::flecs_tools::SnapshotGroupId id {};
  for (int axis = 0; axis < 3; ++axis) {
    id = (id << kChunkAxisBits) | (static_cast<uint64_t>(chunk[axis] + kChunkAxisBias) & kChunkAxisMask);
  }
  return id;
}

void BlockSnapshotSystem::Register(flecs::world& world) {
  OnInitSystems(world, InstallGrouping);
}

}  // namespace z13::building
