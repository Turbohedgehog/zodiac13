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

#include <algorithm>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <Eigen/Core>

#include <lib_core/state/world_serializer.h>
#include <lib_core/utils/log.h>

#include <primitives/placement.h>
#include <sectors/sector.h>
#include <z13/components/block.h>
#include <z13/components/block_changes.h>
#include <z13/components/net.h>
#include <z13_module/gameplay/gameplay_entities.h>
#include <z13_settings/building_tuning.h>
#include <z13_settings/net_tuning.h>

#include "catch_up.h"
#include "wire_records.h"

namespace z13::net {

namespace {

namespace ft = z13::flecs_tools;

// The blocks of `chunks` (snapshot group ids), and with `touching` those whose cells reach into that sector too.
std::vector<flecs::entity> BlocksOf(flecs::world world, std::span<const uint64_t> chunks,
                                    const std::optional<sectors::SectorKey>& touching = std::nullopt) {
  const int chunk_cells = world.get<z13::BuildingTuning>().index_chunk_cells;
  const int sector_cells = world.get<NetTuning>().sector_size_cells;
  const std::unordered_set<uint64_t> wanted_chunks(chunks.begin(), chunks.end());
  const std::optional<z13::building::primitives::CellBox> sector_box =
      touching ? std::optional(z13::building::primitives::CellBox {
                     .min = *touching * sector_cells, .extent = Eigen::Vector3i::Constant(sector_cells)})
               : std::nullopt;
  std::vector<flecs::entity> found;
  world.query<const z13::station::Block>().each([&](flecs::entity e, const z13::station::Block& block) {
    const uint64_t chunk =
        z13::building::primitives::PackChunk(z13::building::primitives::ChunkOf(block.cell, chunk_cells));
    if (wanted_chunks.contains(chunk) ||
        (sector_box && z13::building::primitives::OccupiedCells(block).Overlaps(*sector_box))) {
      found.push_back(e);
    }
  });
  return found;
}

void Destroy(std::span<const flecs::entity> blocks) {
  for (const flecs::entity e : blocks) {
    z13::station::DestroyBlock(e, e.get<z13::station::Block>());
  }
}

bool HoldsAsSent(flecs::world world, flecs::entity block, const ft::EntitySnapshot& sent) {
  const auto held = ft::CaptureEntityState(world, block);
  if (!held) {
    return false;
  }
  const ft::EntityRefs entities = ft::AllEntities(*held);
  return entities.size() == 1 && entities.front().get() == sent;
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

// What touches the sector becomes what the server sent: a block it removed while the client didn't hold the sector
// goes too. One the client already holds as sent stays, since a floor rebuilt under a player's feet would cost it a
// tick of standing on it.
void ApplySectorEntered(flecs::world world, const fbs::net::SectorEnteredT& entered) {
  const ft::WorldSnapshot sent = entered.blocks ? ft::FromFlatbuffer(*entered.blocks) : ft::WorldSnapshot {};
  std::unordered_map<std::string_view, std::reference_wrapper<const ft::EntitySnapshot>> sent_by_name;
  for (const ft::EntitySnapshot& entity : ft::AllEntities(sent)) {
    sent_by_name.emplace(entity.name, entity);
  }
  std::unordered_set<std::string_view> kept;
  std::vector<flecs::entity> doomed;
  for (const flecs::entity block :
       BlocksOf(world, entered.chunks, sectors::SectorKey(entered.sector_x, entered.sector_y, entered.sector_z))) {
    const std::string name = block.path("::", "").c_str();
    const auto found = sent_by_name.find(name);
    if (found != sent_by_name.end() && HoldsAsSent(world, block, found->second)) {
      kept.insert(found->first);
    } else {
      doomed.push_back(block);
    }
  }
  Destroy(doomed);
  ft::WorldSnapshot changed;
  for (const auto& [name, entity] : sent_by_name) {
    if (!kept.contains(name)) {
      changed.entities.push_back(entity);
    }
  }
  std::ranges::sort(changed.entities, {}, &ft::EntitySnapshot::name);
  if (auto applied = ft::ApplyWorldStateDelta(world, changed); !applied) {
    log_warn("NetSession: interest event rejected: {}", applied.error());
  }
}

void ApplySectorLeft(flecs::world world, const fbs::net::SectorLeftT& left) {
  Destroy(BlocksOf(world, left.chunks));
}

void ApplyWorldEdit(flecs::world world, const fbs::net::WorldEditT& edit) {
  if (world.has<z13::net::ServerRole>()) {
    return;
  }
  if (edit.entities) {
    ApplyState(world, edit.entities);
  }
  for (const std::string& name : edit.removed) {
    const flecs::entity e = world.lookup(name.c_str());
    if (!e) {
      continue;
    }
    if (const auto* block = e.try_get<z13::station::Block>()) {
      z13::station::DestroyBlock(e, *block);
    } else {
      e.destruct();
    }
  }
}

}  // namespace z13::net
