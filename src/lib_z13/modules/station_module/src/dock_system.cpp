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

#include "dock_system.h"

#include <algorithm>
#include <optional>
#include <utility>
#include <vector>

#include <flecs.h>

#include <lib_core/state/world_state.h>
#include <lib_core/world/lifecycle.h>

#include <z13/components/station/dock.h>
#include <z13/components/station/grid.h>
#include <z13/components/station/rooms.h>
#include <rooms/room_cache.h>
#include <rooms/room_id.h>

namespace z13::station {

namespace {

using z13::station::rooms::RoomCache;

using GridQuery = flecs::query<const Grid, const RoomCache>;
using DockQuery = flecs::query<const Dock>;

// The room holding the end's cell, if its grid has rooms and the cell is in one rather than in vacuum.
std::optional<RoomRef> RoomOf(const GridQuery& grids, const DockEnd& end) {
  std::optional<RoomRef> found;
  grids.each([&](const Grid& grid, const RoomCache& cache) {
    if (grid.id != end.grid || !cache.Current()) {
      return;
    }
    const auto room = cache.Current()->get().RoomAt(end.cell);
    if (room && *room != rooms::kVacuumRoom) {
      found = RoomRef {.grid = grid.id, .room = *room};
    }
  });
  return found;
}

void LinkDocks(DockLinks& links, const GridQuery& grids, const DockQuery& docks) {
  std::vector<DockLink> list;
  docks.each([&](flecs::entity dock, const Dock& ends) {
    const auto a = RoomOf(grids, ends.a);
    const auto b = RoomOf(grids, ends.b);
    if (a && b) {
      list.push_back({.dock = dock.id(), .a = *a, .b = *b});
    }
  });
  // Entities come in table order, which restores may change.
  std::ranges::sort(list, {}, &DockLink::dock);
  if (links.list != list) {
    links.list = std::move(list);
  }
}

void RegisterComponents(flecs::world world) {
  z13::flecs_tools::RegisterComponentMeta<DockEnd>(world);
  z13::flecs_tools::RegisterComponents<Dock, DockLinks>(world);
}

void RegisterPipeline(flecs::world world) {
  world.component<DockPhase>().add(flecs::Phase).depends_on<StationTopologyPhase>();
}

void RegisterSystems(flecs::world world) {
  world.set(DockLinks {});
  const GridQuery grids = world.query<const Grid, const RoomCache>();
  const DockQuery docks = world.query<const Dock>();
  world.system<DockLinks>("DockSystem::Link")
      .kind<DockPhase>()
      .read<Grid>()
      .read<RoomCache>()
      .read<Dock>()
      .each([grids, docks](DockLinks& links) { LinkDocks(links, grids, docks); });
}

}  // namespace

void DockSystem::Register(flecs::world& world) {
  OnRegisterComponents(world, RegisterComponents);
  OnInitPhases(world, RegisterPipeline);
  OnInitSystems(world, RegisterSystems);
}

}  // namespace z13::station
