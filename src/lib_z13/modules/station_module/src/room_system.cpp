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

#include "room_system.h"

#include <algorithm>
#include <cstdint>
#include <expected>
#include <functional>
#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <lib_core/state/world_serializer.h>
#include <lib_core/state/world_state.h>
#include <lib_core/time/simulation_clock.h>
#include <lib_core/utils/flecs_utils.h>
#include <lib_core/utils/log.h>
#include <lib_core/utils/thread_pool.h>
#include <lib_core/world/lifecycle.h>
#include <lib_profile/profile.h>

#include <z13/components/block_change_reader.h>
#include <z13/components/gameplay_phases.h>
#include <z13/components/held_sectors.h>
#include <z13/components/utilities.h>
#include <z13/components/rooms.h>
#include <z13/components/block.h>
#include <z13/components/grid.h>
#include <z13/components/station_mode.h>
#include <z13_settings/building_tuning.h>
#include <primitives/palette.h>
#include <rooms/parallel_for.h>
#include <rooms/room_builder.h>
#include <rooms/room_cache.h>
#include <rooms/room_update.h>

#include "topology_sources.h"

namespace z13::station {

namespace {

using z13::building::primitives::BlockPalette;
using z13::station::rooms::RoomCache;
using z13::utility::UtilityConsumersPhase;
using z13::utility::UtilityNetworksPhase;
using z13::utility::UtilitySimulationPhase;

using BlockQuery = flecs::query<const Block>;

void RegisterComponents(flecs::world world) {
  z13::flecs_tools::RegisterComponents<TopologyVersion, RoomCache, HeldSectors, StationBounds>(world);
}

void RegisterPipeline(flecs::world world) {
  world.component<StationTopologyPhase>().add(flecs::Phase).depends_on<z13::gameplay::PostUpdatePhase>();
  world.component<UtilityNetworksPhase>().add(flecs::Phase).depends_on<StationTopologyPhase>();
  world.component<UtilitySimulationPhase>().add(flecs::Phase).depends_on<UtilityNetworksPhase>();
  world.component<UtilityConsumersPhase>().add(flecs::Phase).depends_on<UtilitySimulationPhase>();
  // After the utilities, so their systems may read the block changes too.
  world.component<ClearBlockChangesPhase>().add(flecs::Phase).depends_on<UtilityConsumersPhase>();
}

void InstallSingletons(flecs::world world) {
  world.set(HeldSectors {});
  world.set(StationBounds {});
}

rooms::RoomCache::Build FromSources(rooms::RoomSources sources, const rooms::RoomSettings& settings,
                                    rooms::ParallelFor parallel) {
  return [sources = std::move(sources), settings, parallel = std::move(parallel)](rooms::RoomId first_id,
                                                                                   rooms::OptionalRoomGraph current) {
    return current ? rooms::UpdateRooms(*current, sources, first_id, settings, parallel)
                   : rooms::BuildRooms(sources, first_id, settings, parallel);
  };
}

rooms::RoomSources SourcesWithCut(const BlockQuery& blocks, const BlockPalette& palette, const RoomCut& cut,
                                  GridId grid) {
  rooms::RoomSources sources = GatherSources(blocks, palette, grid).sources;
  std::ranges::copy(cut.walls, std::back_inserter(sources.sealed));
  return sources;
}

// `edits_apply`: the current graph is that of the blocks before the edits. Otherwise, or
// when the edits fail, the rooms come from every block and `cut`.
rooms::RoomCache::Build FromEdits(TopologyEdits edits, bool edits_apply, const BlockQuery& blocks, GridId grid,
                                  const BlockPalette& palette, RoomCut cut, const rooms::RoomSettings& settings,
                                  rooms::ParallelFor parallel) {
  return [edits = std::move(edits), edits_apply, &blocks, grid, &palette, cut = std::move(cut), settings,
          parallel = std::move(parallel)](
             rooms::RoomId first_id,
             rooms::OptionalRoomGraph current) -> std::expected<rooms::RoomGraph, std::string> {
    if (current && edits_apply) {
      if (auto updated = rooms::UpdateRooms(*current, edits.edits, first_id, settings, parallel)) {
        return updated;
      }
    }
    return FromSources(SourcesWithCut(blocks, palette, cut, grid), settings, parallel)(first_id, current);
  };
}

// FreeSpace's bounds without their free layer.
StationBounds BoundsOf(const RoomCache& cache) {
  const auto graph = cache.Current();
  if (!graph) {
    return {};
  }
  const z13::building::primitives::CellBox& around = graph->get().space.Bounds();
  if (around.extent.isZero()) {
    return {};
  }
  const z13::building::primitives::CellBox sealed = around.Grown(-Eigen::Vector3i::Ones());
  return {.sealed = SealedBounds {.min = sealed.min, .extent = sealed.extent}};
}

using GridQuery = flecs::query<const Grid>;

// What every grid's update has in common this tick.
struct RoomInputs {
  const BlockChangeBatch& batch;
  const BlockQuery& blocks;
  const BlockPalette& palette;
  const rooms::RoomSettings& settings;
  const z13::ThreadPool& pool;
};

// What changes a grid's rooms: the changes of its blocks, and for the main grid the cut at the edge of the held sectors.
// False if there was nothing to do.
bool UpdateGridRooms(const RoomInputs& in, GridId grid, bool resync, const RoomCut& applied_cut, const RoomCut& cut,
                     bool cut_changed, TopologyVersion& topology, RoomCache& cache) {
  std::vector<BlockChange> changes;
  std::ranges::copy_if(in.batch.changes, std::back_inserter(changes),
                       [grid](const BlockChange& change) { return change.block.grid == grid; });
  if (!resync && changes.empty() && !cut_changed) {
    return false;
  }
  Z13_PROFILE_ZONE("RoomSystem::UpdateRooms");
  const z13::ThreadPool& pool = in.pool;
  rooms::ParallelFor parallel = [pool](size_t count, const std::function<void(size_t)>& body) {
    pool.ForEachIndex(count, body);
  };
  uint64_t fingerprint {};
  rooms::RoomCache::Build build;
  if (resync) {
    TopologySources gathered = GatherSources(in.blocks, in.palette, grid);
    std::ranges::copy(cut.walls, std::back_inserter(gathered.sources.sealed));
    fingerprint = gathered.fingerprint + cut.fingerprint;
    build = FromSources(std::move(gathered.sources), in.settings, std::move(parallel));
  } else {
    TopologyEdits edits = EditsOf(changes, in.palette);
    if (cut_changed) {
      std::ranges::copy(applied_cut.walls, std::back_inserter(edits.edits.removed.sealed));
      std::ranges::copy(cut.walls, std::back_inserter(edits.edits.added.sealed));
      edits.fingerprint_change += cut.fingerprint - applied_cut.fingerprint;
    }
    fingerprint = topology.fingerprint + edits.fingerprint_change;
    const bool edits_apply = cache.CurrentFingerprint() == topology.fingerprint;
    build = FromEdits(std::move(edits), edits_apply, in.blocks, grid, in.palette, cut, in.settings, std::move(parallel));
  }
  if (topology.fingerprint != fingerprint) {
    topology.fingerprint = fingerprint;
    ++topology.version;
  }
  const auto graph = cache.Select(fingerprint, build);
  if (!graph) {
    log_error("station: no rooms in grid {}: {}", grid, graph.error());
  }
  return true;
}

// The grids in id order, so every peer updates them alike.
std::vector<flecs::entity> GridsInOrder(const GridQuery& grids) {
  std::vector<flecs::entity> entities;
  grids.each([&entities](flecs::entity entity, const Grid&) { entities.push_back(entity); });
  std::ranges::sort(entities, {}, [](const flecs::entity& entity) { return entity.get<Grid>().id; });
  return entities;
}

// What the rooms system keeps between ticks.
struct RoomMemory {
  BlockChangeReader reader;
  // The cut the main grid's rooms were last built with.
  RoomCut applied_cut;
};

void UpdateRooms(RoomMemory& memory, const GridQuery& grids, const BlockQuery& blocks, const HeldSectors& held,
                 uint64_t restores, uint64_t tick, StationBounds& bounds, const BlockPalette& palette,
                 const z13::BuildingTuning& tuning, const z13::ThreadPool& pool) {
  const BlockChangeBatch batch = memory.reader.Read(restores, tick);
  const rooms::RoomSettings settings = RoomSettingsOf(tuning);
  const RoomInputs inputs {.batch = batch, .blocks = blocks, .palette = palette, .settings = settings, .pool = pool};
  const bool cut_changed = held != memory.applied_cut.held;
  const RoomCut main_cut = cut_changed ? CutOf(held) : memory.applied_cut;
  const RoomCut no_cut;
  for (flecs::entity grid : GridsInOrder(grids)) {
    const GridId id = grid.get<Grid>().id;
    // A grid new to the rooms gets its components at once, and its rooms are built whole from its blocks.
    const bool fresh = !grid.has<RoomCache>() || !grid.has<TopologyVersion>();
    if (fresh) {
      flecs::world world = grid.world();
      const z13::ImmediateScope immediate(world);
      if (!grid.has<RoomCache>()) {
        grid.set(RoomCache {});
      }
      if (!grid.has<TopologyVersion>()) {
        grid.set(TopologyVersion {});
      }
    }
    const bool is_main = id == kMainGrid;
    const bool resync = batch.resync || fresh;
    RoomCache& cache = grid.get_mut<RoomCache>();
    const bool updated =
        UpdateGridRooms(inputs, id, resync, is_main ? memory.applied_cut : no_cut, is_main ? main_cut : no_cut,
                        is_main && cut_changed, grid.get_mut<TopologyVersion>(), cache);
    if (is_main && updated) {
      memory.applied_cut = main_cut;
      // A cut world's walls would read as blocks.
      bounds = held.sectors ? StationBounds {} : BoundsOf(cache);
    }
  }
}

void RegisterSystems(flecs::world world) {
  InstallSingletons(world);

  const auto memory = std::make_shared<RoomMemory>(RoomMemory {.reader = BlockChangeReader(world)});
  const GridQuery grids = world.query<const Grid>();
  const BlockQuery blocks = world.query<const Block>();
  world.system<StationBounds, const HeldSectors, const z13::flecs_tools::WorldRestores,
               const z13::flecs_tools::SimulationClock, const BlockPalette, const z13::BuildingTuning,
               const z13::ThreadPool>("RoomSystem::UpdateRooms")
      .kind<StationTopologyPhase>()
      // Immediate, so a grid new to the rooms can get its components at once (ImmediateScope).
      .immediate()
      .with<StationMode>()
      .read<Block>()
      .read<BlockChange>()
      .read<Grid>()
      .write<RoomCache>()
      .write<TopologyVersion>()
      .each([memory, grids, blocks](flecs::iter&, size_t, StationBounds& bounds, const HeldSectors& held,
                                    const z13::flecs_tools::WorldRestores& restores,
                                    const z13::flecs_tools::SimulationClock& clock, const BlockPalette& palette,
                                    const z13::BuildingTuning& tuning, const z13::ThreadPool& pool) {
        UpdateRooms(*memory, grids, blocks, held, restores.count, clock.tick, bounds, palette, tuning, pool);
      });
}

}  // namespace

void RoomSystem::Register(flecs::world& world) {
  OnRegisterComponents(world, RegisterComponents);

  OnInitPhases(world, RegisterPipeline);

  OnInitSystems(world, RegisterSystems);
}

}  // namespace z13::station
