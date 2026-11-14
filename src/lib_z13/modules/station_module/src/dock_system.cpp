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
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include <flecs.h>

#include <lib_core/state/world_state.h>
#include <lib_core/world/lifecycle.h>

#include <grid/block_index.h>
#include <z13/components/station/dock.h>
#include <z13/components/station/door.h>
#include <z13/components/station/grid.h>
#include <z13/components/station/rooms.h>
#include <primitives/cell_box.h>
#include <rooms/room_cache.h>
#include <rooms/room_id.h>

namespace z13::station {

namespace {

using z13::station::rooms::RoomCache;

using z13::building::grid::BlockIndex;
using z13::station::DoorState;
using GridQuery = flecs::query<const Grid, const RoomCache>;
using DockQuery = flecs::query<const Dock>;

struct EndRoom {
  RoomRef room;
  DockDoor door;
};

// The squared distance in cells from `cell` to the nearest cell of `box`.
int64_t DistanceSquared(const Eigen::Vector3i& cell, const z13::building::primitives::CellBox& box) {
  const Eigen::Vector3i last = box.End() - Eigen::Vector3i::Ones();
  return (cell - cell.cwiseMax(box.min).cwiseMin(last)).cast<int64_t>().squaredNorm();
}

// Doors are told apart by name: ids differ between peers, names do not.
bool NameBefore(const flecs::world& world, flecs::entity_t a, flecs::entity_t b) {
  return std::string_view(world.entity(a).name().c_str()) < std::string_view(world.entity(b).name().c_str());
}

// The door among the portals of `room` whose opening is nearest `cell`: the door block is found by the index.
DockDoor NearestDoor(flecs::entity grid, const rooms::RoomGraph& graph, rooms::RoomId room, const Eigen::Vector3i& cell) {
  const auto* index = grid.try_get<BlockIndex>();
  DockDoor nearest;
  if (index == nullptr) {
    return nearest;
  }
  int64_t best = std::numeric_limits<int64_t>::max();
  for (const rooms::Portal& portal : graph.portals) {
    if (portal.a != room && portal.b != room) {
      continue;
    }
    const int64_t distance = DistanceSquared(cell, portal.opening);
    for (const auto block : index->Overlapping(portal.opening)) {
      if (!grid.world().entity(block).has<DoorState>()) {
        continue;
      }
      if (distance < best || (distance == best && NameBefore(grid.world(), block, nearest.door))) {
        best = distance;
        nearest = {.door = block, .area_cells = portal.area_cells};
      }
    }
  }
  return nearest;
}

// The room holding the end's cell, if its grid has rooms and the cell is in one rather than in vacuum.
std::optional<EndRoom> RoomOf(const GridQuery& grids, const DockEnd& end) {
  std::optional<EndRoom> found;
  grids.each([&](flecs::entity entity, const Grid& grid, const RoomCache& cache) {
    if (grid.id != end.grid || !cache.Current()) {
      return;
    }
    const rooms::RoomGraph& graph = cache.Current()->get();
    const auto room = graph.RoomAt(end.cell);
    if (room && *room != rooms::kVacuumRoom) {
      found = EndRoom {.room = {.grid = grid.id, .room = *room}, .door = NearestDoor(entity, graph, *room, end.cell)};
    }
  });
  return found;
}

using LinkBasis = std::vector<std::pair<GridId, std::optional<uint64_t>>>;

LinkBasis BasisOf(const GridQuery& grids) {
  LinkBasis basis;
  grids.each([&basis](const Grid& grid, const RoomCache& cache) {
    basis.emplace_back(grid.id, cache.CurrentFingerprint());
  });
  std::ranges::sort(basis);
  return basis;
}

// A link without both doors may be one whose door block has no DoorState yet, so it is made again.
bool HasLiveDoors(const flecs::world& world, const std::vector<DockLink>& list) {
  return std::ranges::all_of(list, [&world](const DockLink& link) {
    return link.door_a.door != 0 && link.door_b.door != 0 && world.is_alive(link.door_a.door) &&
           world.is_alive(link.door_b.door);
  });
}

void LinkDocks(const flecs::world& world, DockLinks& links, const GridQuery& grids, const DockQuery& docks, std::optional<LinkBasis>& made_from) {
  // Finding each end's nearest door walks the portals, which a quiet frame has no need of.
  LinkBasis basis = BasisOf(grids);
  const bool docks_changed = docks.changed();
  if (!docks_changed && made_from == basis && HasLiveDoors(world, links.list)) {
    return;
  }
  made_from = std::move(basis);
  std::vector<DockLink> list;
  docks.each([&](flecs::entity dock, const Dock& ends) {
    const auto a = RoomOf(grids, ends.a);
    const auto b = RoomOf(grids, ends.b);
    if (a && b) {
      list.push_back({.dock = dock.id(), .a = a->room, .b = b->room, .door_a = a->door, .door_b = b->door});
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
  const DockQuery docks = world.query_builder<const Dock>().detect_changes().build();
  const auto made_from = std::make_shared<std::optional<LinkBasis>>();
  world.system<DockLinks>("DockSystem::Link")
      .kind<DockPhase>()
      .read<Grid>()
      .read<RoomCache>()
      .read<Dock>()
      .read<BlockIndex>()
      .read<DoorState>()
      .each([grids, docks, made_from](flecs::iter& it, size_t, DockLinks& links) {
        LinkDocks(it.world(), links, grids, docks, *made_from);
      });
}

}  // namespace

void DockSystem::Register(flecs::world& world) {
  OnRegisterComponents(world, RegisterComponents);
  OnInitPhases(world, RegisterPipeline);
  OnInitSystems(world, RegisterSystems);
}

}  // namespace z13::station
