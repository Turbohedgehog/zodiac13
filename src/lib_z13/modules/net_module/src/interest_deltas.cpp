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

#include "interest_deltas.h"

#include <vector>

#include <Eigen/Core>

#include <lib_core/state/world_serializer.h>
#include <lib_core/utils/log.h>

#include <sectors/sector.h>
#include <z13/components/block.h>
#include <z13/components/block_changes.h>
#include <z13_module/gameplay/gameplay_entities.h>
#include <z13_settings/net_tuning.h>

#include "catch_up.h"
#include "wire_records.h"

namespace z13::net {

namespace {

namespace ft = z13::flecs_tools;

void DestroyBlocksIn(flecs::world world, const sectors::SectorKey& sector) {
  const int sector_cells = world.get<NetTuning>().sector_size_cells;
  std::vector<flecs::entity> doomed;
  world.query<const z13::station::Block>().each(
      [&](flecs::entity e, const z13::station::Block& block) {
        if (sectors::SectorOf(block.cell, sector_cells) == sector) {
          doomed.push_back(e);
        }
      });
  for (const flecs::entity e : doomed) {
    z13::station::DestroyBlock(e, e.get<z13::station::Block>());
  }
}

void ApplyState(flecs::world world, const std::unique_ptr<fbs::state::WorldSnapshotT>& state) {
  if (!state) {
    log_warn("NetSession: an interest event without state");
    return;
  }
  if (auto applied = ft::ApplyWorldStateDelta(world, ft::FromFlatbuffer(*state)); !applied) {
    log_warn("NetSession: interest event rejected: {}", applied.error());
  }
}

}  // namespace

void ApplyEntityEntered(flecs::world world, const fbs::net::EntityEnteredT& entered) {
  ApplyState(world, entered.entity_state);
  MergePlayerInput(world, {.held_values = FromWire(entered.held_values),
                           .pending = FromWire(entered.pending),
                           .confirmed = FromWire(entered.input_through)});
}

void ApplyEntityLeft(flecs::world world, const fbs::net::EntityLeftT& left) {
  if (const flecs::entity e = world.lookup(z13::gameplay::PlayerEntityName(left.player_id).c_str())) {
    e.destruct();
  }
}

void ApplySectorEntered(flecs::world world, const fbs::net::SectorEnteredT& entered) {
  DestroyBlocksIn(world, {entered.sector_x, entered.sector_y, entered.sector_z});
  ApplyState(world, entered.blocks);
}

void ApplySectorLeft(flecs::world world, const fbs::net::SectorLeftT& left) {
  DestroyBlocksIn(world, {left.sector_x, left.sector_y, left.sector_z});
}

}  // namespace z13::net
