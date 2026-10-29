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

#include "sector_system.h"

#include <memory>
#include <unordered_set>

#include <Eigen/Dense>

#include <lib_core/state/world_serializer.h>
#include <lib_core/state/world_state.h>
#include <lib_core/time/simulation_clock.h>
#include <lib_core/utils/math.h>
#include <lib_core/world/lifecycle.h>
#include <lib_profile/profile.h>

#include <primitives/placement.h>
#include <sectors/sector.h>
#include <sectors/sector_index.h>
#include <z13/components/block.h>
#include <z13/components/block_change_reader.h>
#include <z13/components/gameplay_phases.h>
#include <z13/components/player.h>
#include <z13_settings/net_tuning.h>

namespace z13::net {

namespace {

using sectors::MemberKind;
using sectors::SectorIndex;
using sectors::SectorMember;
using z13::station::BlockChangeKind;

using PlayerQuery = flecs::query<const z13::gameplay::Player, const Eigen::Matrix4f>;

void RegisterPipeline(flecs::world world) {
  world.component<SectorPhase>().add(flecs::Phase).depends_on<z13::gameplay::PostUpdatePhase>();
  world.component<z13::station::ClearBlockChangesPhase>().add(flecs::Phase).depends_on<SectorPhase>();
}

void RegisterComponents(flecs::world world) {
  z13::flecs_tools::RegisterComponents<SectorIndex>(world);
}

SectorMember BlockMember(flecs::entity_t entity) {
  return {.kind = MemberKind::kBlock, .id = entity};
}

void PlaceBlock(SectorIndex& index, flecs::entity_t entity, const z13::station::Block& block, int sector_cells) {
  index.Place(
      BlockMember(entity), sectors::SectorsTouched(z13::building::primitives::OccupiedCells(block), sector_cells));
}

void RebuildBlocks(const z13::station::BlockChangeReader& reader, SectorIndex& index, int sector_cells) {
  for (const uint64_t id : index.IdsOf(MemberKind::kBlock)) {
    index.Remove(BlockMember(id));
  }
  reader.Blocks().each([&index, sector_cells](flecs::entity block, const z13::station::Block& data) {
    PlaceBlock(index, block.id(), data, sector_cells);
  });
}

void SyncBlocks(z13::station::BlockChangeReader& reader, uint64_t restores, uint64_t tick, SectorIndex& index,
                int sector_cells, bool regridded) {
  const z13::station::BlockChangeBatch batch = reader.Read(restores, tick);
  if (batch.resync || regridded) {
    RebuildBlocks(reader, index, sector_cells);
    return;
  }
  for (const z13::station::BlockChange& change : batch.changes) {
    if (change.kind == BlockChangeKind::kRemoved) {
      index.Remove(BlockMember(change.entity));
    } else {
      PlaceBlock(index, change.entity, change.block, sector_cells);
    }
  }
}

void SyncPlayers(const PlayerQuery& players, SectorIndex& index, int sector_cells) {
  std::unordered_set<uint64_t> present;
  players.each([&](const z13::gameplay::Player& player, const Eigen::Matrix4f& transform) {
    const Eigen::Vector3i cell =
        (z13::math::ExtractTranslation<float>(transform) / z13::station::kCellSize).array().floor().cast<int>();
    index.Place({.kind = MemberKind::kPlayer, .id = player.id}, sectors::SectorOf(cell, sector_cells));
    present.insert(player.id);
  });
  for (const uint64_t id : index.IdsOf(MemberKind::kPlayer)) {
    if (!present.contains(id)) {
      index.Remove({.kind = MemberKind::kPlayer, .id = id});
    }
  }
}

void RegisterSystems(flecs::world world) {
  world.set(SectorIndex {});

  const auto reader = std::make_shared<z13::station::BlockChangeReader>(world);
  const PlayerQuery players = world.query<const z13::gameplay::Player, const Eigen::Matrix4f>();
  world.system<SectorIndex, const z13::flecs_tools::WorldRestores, const z13::flecs_tools::SimulationClock,
               const z13::NetTuning>("SectorSystem::Sync")
      .kind<SectorPhase>()
      .read<z13::station::Block>()
      .read<z13::station::BlockChange>()
      .read<z13::gameplay::Player>()
      .read<Eigen::Matrix4f>()
      .each([reader, players](flecs::iter&, size_t, SectorIndex& index,
                              const z13::flecs_tools::WorldRestores& restores,
                              const z13::flecs_tools::SimulationClock& clock, const z13::NetTuning& tuning) {
        Z13_PROFILE_ZONE("SectorSystem::Sync");
        const int sector_cells = tuning.sector_size_cells;
        const bool regridded = index.SectorCells() != sector_cells;
        if (regridded) {
          index = SectorIndex(sector_cells);
        }
        SyncBlocks(*reader, restores.count, clock.tick, index, sector_cells, regridded);
        SyncPlayers(players, index, sector_cells);
      });
}

}  // namespace

void SectorSystem::Register(flecs::world& world) {
  z13::OnRegisterComponents(world, RegisterComponents);
  z13::OnInitPhases(world, RegisterPipeline);
  z13::OnInitSystems(world, RegisterSystems);
}

}  // namespace z13::net
