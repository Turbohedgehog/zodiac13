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
#include <lib_core/utils/log.h>
#include <lib_core/utils/thread_pool.h>
#include <lib_core/world/lifecycle.h>
#include <lib_profile/profile.h>

#include <z13/components/block_change_reader.h>
#include <z13/components/gameplay_phases.h>
#include <z13/components/held_sectors.h>
#include <z13/components/rooms.h>
#include <z13/components/block.h>
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

using BlockQuery = flecs::query<const Block>;

void RegisterComponents(flecs::world world) {
  z13::flecs_tools::RegisterComponents<TopologyVersion, RoomCache, HeldSectors, StationBounds>(world);
}

void RegisterPipeline(flecs::world world) {
  world.component<StationTopologyPhase>().add(flecs::Phase).depends_on<z13::gameplay::PostUpdatePhase>();
  world.component<ClearBlockChangesPhase>().add(flecs::Phase).depends_on<StationTopologyPhase>();
}

void InstallSingletons(flecs::world world) {
  world.set(TopologyVersion {});
  world.set(RoomCache {});
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

rooms::RoomSources SourcesWithCut(const BlockQuery& blocks, const BlockPalette& palette, const RoomCut& cut) {
  rooms::RoomSources sources = GatherSources(blocks, palette).sources;
  std::ranges::copy(cut.walls, std::back_inserter(sources.sealed));
  return sources;
}

// `edits_apply`: the current graph is that of the blocks before the edits. Otherwise, or
// when the edits fail, the rooms come from every block and `cut`.
rooms::RoomCache::Build FromEdits(TopologyEdits edits, bool edits_apply, const BlockQuery& blocks,
                                  const BlockPalette& palette, RoomCut cut, const rooms::RoomSettings& settings,
                                  rooms::ParallelFor parallel) {
  return [edits = std::move(edits), edits_apply, &blocks, &palette, cut = std::move(cut), settings,
          parallel = std::move(parallel)](
             rooms::RoomId first_id,
             rooms::OptionalRoomGraph current) -> std::expected<rooms::RoomGraph, std::string> {
    if (current && edits_apply) {
      if (auto updated = rooms::UpdateRooms(*current, edits.edits, first_id, settings, parallel)) {
        return updated;
      }
    }
    return FromSources(SourcesWithCut(blocks, palette, cut), settings, parallel)(first_id, current);
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

void UpdateRooms(BlockChangeReader& reader, RoomCut& applied_cut, const HeldSectors& held, uint64_t restores,
                 uint64_t tick, TopologyVersion& topology, RoomCache& cache, StationBounds& bounds,
                 const BlockPalette& palette, const z13::BuildingTuning& tuning, const z13::ThreadPool& pool) {
  const BlockChangeBatch batch = reader.Read(restores, tick);
  const bool cut_changed = held != applied_cut.held;
  if (!batch.resync && batch.changes.empty() && !cut_changed) {
    return;
  }
  Z13_PROFILE_ZONE("RoomSystem::UpdateRooms");
  const rooms::RoomSettings settings = RoomSettingsOf(tuning);
  rooms::ParallelFor parallel = [pool](size_t count, const std::function<void(size_t)>& body) {
    pool.ForEachIndex(count, body);
  };
  uint64_t fingerprint {};
  rooms::RoomCache::Build build;
  RoomCut cut = cut_changed ? CutOf(held) : applied_cut;
  if (batch.resync) {
    TopologySources gathered = GatherSources(reader.Blocks(), palette);
    std::ranges::copy(cut.walls, std::back_inserter(gathered.sources.sealed));
    fingerprint = gathered.fingerprint + cut.fingerprint;
    build = FromSources(std::move(gathered.sources), settings, std::move(parallel));
  } else {
    TopologyEdits edits = EditsOf(batch.changes, palette);
    if (cut_changed) {
      std::ranges::copy(applied_cut.walls, std::back_inserter(edits.edits.removed.sealed));
      std::ranges::copy(cut.walls, std::back_inserter(edits.edits.added.sealed));
      edits.fingerprint_change += cut.fingerprint - applied_cut.fingerprint;
    }
    fingerprint = topology.fingerprint + edits.fingerprint_change;
    const bool edits_apply = cache.CurrentFingerprint() == topology.fingerprint;
    build = FromEdits(std::move(edits), edits_apply, reader.Blocks(), palette, cut, settings, std::move(parallel));
  }
  applied_cut = std::move(cut);
  if (topology.fingerprint != fingerprint) {
    topology.fingerprint = fingerprint;
    ++topology.version;
  }
  const auto graph = cache.Select(fingerprint, build);
  if (!graph) {
    log_error("station: no rooms: {}", graph.error());
  }
  // A cut world's walls would read as blocks.
  bounds = held.sectors ? StationBounds {} : BoundsOf(cache);
}

void RegisterSystems(flecs::world world) {
  InstallSingletons(world);

  const auto reader = std::make_shared<BlockChangeReader>(world);
  const auto applied_cut = std::make_shared<RoomCut>();
  world.system<TopologyVersion, RoomCache, StationBounds, const HeldSectors, const z13::flecs_tools::WorldRestores,
               const z13::flecs_tools::SimulationClock, const BlockPalette, const z13::BuildingTuning,
               const z13::ThreadPool>(
      "RoomSystem::UpdateRooms")
      .kind<StationTopologyPhase>()
      .with<StationMode>()
      .read<Block>()
      .read<BlockChange>()
      .each([reader, applied_cut](
                flecs::iter&, size_t, TopologyVersion& topology, RoomCache& cache, StationBounds& bounds,
                const HeldSectors& held,
                const z13::flecs_tools::WorldRestores& restores, const z13::flecs_tools::SimulationClock& clock,
                const BlockPalette& palette, const z13::BuildingTuning& tuning, const z13::ThreadPool& pool) {
        UpdateRooms(
            *reader, *applied_cut, held, restores.count, clock.tick, topology, cache, bounds, palette, tuning, pool);
      });
}

}  // namespace

void RoomSystem::Register(flecs::world& world) {
  OnRegisterComponents(world, RegisterComponents);

  OnInitPhases(world, RegisterPipeline);

  OnInitSystems(world, RegisterSystems);
}

}  // namespace z13::station
