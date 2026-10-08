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

#include <lib_core/state/world_state.h>
#include <lib_core/utils/log.h>
#include <lib_core/world/lifecycle.h>

#include <z13/components/gameplay.h>
#include <z13/components/rooms.h>
#include <z13/components/station.h>
#include <primitives/palette.h>
#include <rooms/room_builder.h>
#include <rooms/room_cache.h>

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
}

void InstallSingletons(flecs::world world) {
  world.set(TopologyVersion {});
  world.set(RoomCache {});
}

// The count catches a removal change detection misses.
bool BlocksChanged(const BlockQuery& blocks, const RoomCache& cache) {
  const bool changed = blocks.changed();
  return changed || static_cast<size_t>(blocks.count()) != cache.BlocksSeen();
}

void UpdateRooms(const BlockQuery& blocks, TopologyVersion& topology, RoomCache& cache, const BlockPalette& palette) {
  if (!BlocksChanged(blocks, cache)) {
    return;
  }
  const TopologySources gathered = GatherSources(blocks, palette);
  if (topology.fingerprint != gathered.fingerprint) {
    topology.fingerprint = gathered.fingerprint;
    ++topology.version;
  }
  const auto graph = cache.Select(gathered.fingerprint, [&gathered] { return rooms::BuildRooms(gathered.sources); });
  if (!graph) {
    log_error("station: no rooms: {}", graph.error());
  }
  cache.SetBlocksSeen(static_cast<size_t>(blocks.count()));
}

void RegisterSystems(flecs::world world) {
  InstallSingletons(world);

  const BlockQuery blocks = world.query_builder<const Block>("RoomSystem::BlockQuery").detect_changes().build();
  world.system<TopologyVersion, RoomCache, const BlockPalette>("RoomSystem::UpdateRooms")
      .kind<StationTopologyPhase>()
      .with<StationMode>()
      .read<Block>()
      .each([blocks](flecs::iter&, size_t, TopologyVersion& topology, RoomCache& cache, const BlockPalette& palette) {
        UpdateRooms(blocks, topology, cache, palette);
      });
}

}  // namespace

void RoomSystem::Register(flecs::world& world) {
  OnRegisterComponents(world, RegisterComponents);

  OnInitPhases(world, RegisterPipeline);

  OnInitSystems(world, RegisterSystems);
}

}  // namespace z13::station
