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

#include <flecs.h>

#include <lib_core/state/snapshot_grouping.h>
#include <lib_core/world/lifecycle.h>

#include <z13/components/station/block.h>
#include <primitives/block_group.h>
#include <primitives/placement.h>
#include <z13_settings/building_tuning.h>

namespace z13::building {

namespace {

using z13::station::Block;

z13::flecs_tools::GroupLayout MakeLayout(flecs::world world) {
  const int chunk_cells = world.get<z13::BuildingTuning>().index_chunk_cells;
  return {
      .group_of =
          [chunk_cells](flecs::entity e) {
            const Block& block = e.get<Block>();
            return z13::building::primitives::BlockGroupId(
                block.grid, z13::building::primitives::ChunkOf(block.cell, chunk_cells));
          },
      .key = static_cast<uint64_t>(chunk_cells),
  };
}

// In the systems stage, not next to the registration (see PhysicsSystem::RegisterSystems).
void InstallGrouping(flecs::world world) {
  world.set(z13::flecs_tools::SnapshotGrouping {
      .member = world.component<Block>().id(),
      .make_layout = MakeLayout,
  });
  z13::flecs_tools::PrimeSnapshotGrouping(world);
}

}  // namespace

void BlockSnapshotSystem::Register(flecs::world& world) {
  OnInitSystems(world, InstallGrouping);
}

}  // namespace z13::building
