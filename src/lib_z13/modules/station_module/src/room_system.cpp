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

#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <string>
#include <utility>

#include <lib_core/state/world_serializer.h>
#include <lib_core/state/world_state.h>
#include <lib_core/time/simulation_clock.h>
#include <lib_core/utils/log.h>
#include <lib_core/world/lifecycle.h>
#include <lib_profile/profile.h>

#include <z13/components/block_change_reader.h>
#include <z13/components/gameplay_phases.h>
#include <z13/components/rooms.h>
#include <z13/components/block.h>
#include <z13/components/station_mode.h>
#include <z13_settings/building_tuning.h>
#include <primitives/palette.h>
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
  z13::flecs_tools::RegisterComponents<TopologyVersion, RoomCache>(world);
}

void RegisterPipeline(flecs::world world) {
  world.component<StationTopologyPhase>().add(flecs::Phase).depends_on<z13::gameplay::PostUpdatePhase>();
  world.component<ClearBlockChangesPhase>().add(flecs::Phase).depends_on<StationTopologyPhase>();
}

void InstallSingletons(flecs::world world) {
  world.set(TopologyVersion {});
  world.set(RoomCache {});
}

rooms::RoomCache::Build FromSources(rooms::RoomSources sources, const rooms::RoomSettings& settings) {
  return [sources = std::move(sources), settings](rooms::RoomId first_id, rooms::OptionalRoomGraph current) {
    return current ? rooms::UpdateRooms(*current, sources, first_id, settings)
                   : rooms::BuildRooms(sources, first_id, settings);
  };
}

// `edits_apply`: the current graph is that of the blocks before the edits. Otherwise, or
// when the edits fail, the rooms come from every block.
rooms::RoomCache::Build FromEdits(TopologyEdits edits, bool edits_apply, const BlockQuery& blocks,
                                  const BlockPalette& palette, const rooms::RoomSettings& settings) {
  return [edits = std::move(edits), edits_apply, &blocks, &palette, settings](
             rooms::RoomId first_id,
             rooms::OptionalRoomGraph current) -> std::expected<rooms::RoomGraph, std::string> {
    if (current && edits_apply) {
      if (auto updated = rooms::UpdateRooms(*current, edits.edits, first_id, settings)) {
        return updated;
      }
    }
    return FromSources(GatherSources(blocks, palette).sources, settings)(first_id, current);
  };
}

void UpdateRooms(BlockChangeReader& reader, uint64_t restores, uint64_t tick, TopologyVersion& topology,
                 RoomCache& cache, const BlockPalette& palette, const z13::BuildingTuning& tuning) {
  const BlockChangeBatch batch = reader.Read(restores, tick);
  if (!batch.resync && batch.changes.empty()) {
    return;
  }
  Z13_PROFILE_ZONE("RoomSystem::UpdateRooms");
  const rooms::RoomSettings settings = RoomSettingsOf(tuning);
  uint64_t fingerprint {};
  rooms::RoomCache::Build build;
  if (batch.resync) {
    TopologySources gathered = GatherSources(reader.Blocks(), palette);
    fingerprint = gathered.fingerprint;
    build = FromSources(std::move(gathered.sources), settings);
  } else {
    TopologyEdits edits = EditsOf(batch.changes, palette);
    fingerprint = topology.fingerprint + edits.fingerprint_change;
    const bool edits_apply = cache.CurrentFingerprint() == topology.fingerprint;
    build = FromEdits(std::move(edits), edits_apply, reader.Blocks(), palette, settings);
  }
  if (topology.fingerprint != fingerprint) {
    topology.fingerprint = fingerprint;
    ++topology.version;
  }
  if (const auto graph = cache.Select(fingerprint, build); !graph) {
    log_error("station: no rooms: {}", graph.error());
  }
}

void RegisterSystems(flecs::world world) {
  InstallSingletons(world);

  const auto reader = std::make_shared<BlockChangeReader>(world);
  world.system<TopologyVersion, RoomCache, const z13::flecs_tools::WorldRestores,
               const z13::flecs_tools::SimulationClock, const BlockPalette, const z13::BuildingTuning>(
      "RoomSystem::UpdateRooms")
      .kind<StationTopologyPhase>()
      .with<StationMode>()
      .read<Block>()
      .read<BlockChange>()
      .each([reader](flecs::iter&, size_t, TopologyVersion& topology, RoomCache& cache,
                     const z13::flecs_tools::WorldRestores& restores, const z13::flecs_tools::SimulationClock& clock,
                     const BlockPalette& palette, const z13::BuildingTuning& tuning) {
        UpdateRooms(*reader, restores.count, clock.tick, topology, cache, palette, tuning);
      });
}

}  // namespace

void RoomSystem::Register(flecs::world& world) {
  OnRegisterComponents(world, RegisterComponents);

  OnInitPhases(world, RegisterPipeline);

  OnInitSystems(world, RegisterSystems);
}

}  // namespace z13::station
