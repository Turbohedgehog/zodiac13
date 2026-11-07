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

#include "room_air_store.h"

#include <algorithm>
#include <iterator>
#include <format>
#include <string>

#include <lib_core/state/state_markers.h>
#include <lib_core/utils/flecs_utils.h>

#include <rooms/room_graph.h>

namespace z13::atmos_module {

namespace {

using z13::utility::RoomAirEntry;

std::string NameOf(const RoomAirEntry& room) {
  const auto& anchor = room.anchor;
  if (room.grid == z13::station::kMainGrid) {
    return std::format("RoomAir_{}_{}_{}", anchor.x(), anchor.y(), anchor.z());
  }
  return std::format("RoomAir_g{}_{}_{}_{}", room.grid, anchor.x(), anchor.y(), anchor.z());
}

bool Before(const RoomAirEntry& a, const RoomAirEntry& b) {
  return a.grid != b.grid ? a.grid < b.grid : z13::station::rooms::AnchorBefore(a.anchor, b.anchor);
}

}  // namespace

std::vector<RoomAirEntry> LoadRoomAir(const RoomAirQuery& query) {
  std::vector<RoomAirEntry> rooms;
  query.each([&rooms](const RoomAirEntry& entry) { rooms.push_back(entry); });
  std::ranges::sort(rooms, Before);
  return rooms;
}

std::vector<RoomAirEntry> RoomsOfGrid(const std::vector<RoomAirEntry>& rooms, z13::station::GridId grid) {
  std::vector<RoomAirEntry> of_grid;
  std::ranges::copy_if(rooms, std::back_inserter(of_grid),
                       [grid](const RoomAirEntry& room) { return room.grid == grid; });
  return of_grid;
}

void StoreRoomAir(flecs::world world, const RoomAirQuery& query, const std::vector<RoomAirEntry>& rooms,
                  std::span<const z13::station::GridId> grids) {
  const z13::ImmediateScope immediate(world);
  std::vector<bool> held(rooms.size());
  std::vector<flecs::entity> stale;
  query.each([&](flecs::entity entity, const RoomAirEntry& entry) {
    if (!std::ranges::contains(grids, entry.grid)) {
      return;
    }
    const auto found = std::ranges::lower_bound(rooms, entry, Before);
    if (found == rooms.end() || found->grid != entry.grid || found->anchor != entry.anchor) {
      stale.push_back(entity);
      return;
    }
    held[static_cast<size_t>(found - rooms.begin())] = true;
    if (!(*found == entry)) {
      entity.set(*found);
    }
  });
  std::ranges::for_each(stale, [](flecs::entity entity) { entity.destruct(); });
  for (size_t i = 0; i < rooms.size(); ++i) {
    if (!held[i]) {
      world.entity(NameOf(rooms[i]).c_str()).add<z13::flecs_tools::StateEntity>().set(rooms[i]);
    }
  }
}

}  // namespace z13::atmos_module
