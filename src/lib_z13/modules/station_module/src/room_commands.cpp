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


#include "room_commands.h"

#include <algorithm>
#include <cstdint>
#include <vector>

#include <lib_core/commands/command_registry.h>
#include <lib_core/state/world_state.h>
#include <z13/components/station/grid.h>
#include <rooms/room_cache.h>

namespace z13::station {

namespace {

struct CellInfo {
  int x {};
  int y {};
  int z {};
};

struct RoomInfo {
  uint32_t id {};
  CellInfo min;  // the room's bounding box, in cells
  CellInfo size;
  int64_t volume_cells {};
  int portals {};            // doors, windows and other openings to other rooms
  bool open_to_space {};     // some opening leads to the vacuum: the room leaks
};

struct RoomsCommand {
  using Command = z13::Command<
      std::vector<RoomInfo>,
      "Lists the main grid's rooms (the free volumes the walls close off): id, bounding box in cells, volume in "
      "cells, how many openings it has and whether one of them leads to open space. An empty list means no room "
      "is closed yet.",
      Tags<kDebugTag>>;
  int unused {};
};

struct OpeningInfo {
  uint32_t room {};  // the room that leaks
  CellInfo min;      // the opening's box, in cells
  CellInfo size;
  int area_cells {};
  bool passable {};
};

struct VacuumOpeningsCommand {
  using Command = z13::Command<
      std::vector<OpeningInfo>,
      "Lists the openings of the main grid's rooms that lead to the vacuum, where the station leaks: the room, the "
      "opening's box in cells, its area and whether a body can pass.",
      Tags<kDebugTag>>;
  int unused {};
};

void ListRooms(flecs::world world, ReplyOf<RoomsCommand>& reply) {
  const auto cache = MainGridData<rooms::RoomCache>(world);
  if (!cache || !cache->get().Current()) {
    reply.Succeed({});
    return;
  }
  const rooms::RoomGraph& graph = cache->get().Current()->get();
  std::vector<RoomInfo> listed;
  for (const rooms::RoomId id : graph.ByAnchor()) {
    const rooms::Room& room = graph.rooms.at(id);
    const auto portals = graph.PortalsOf(id);
    const bool leaks = std::ranges::any_of(portals, [&graph](size_t index) {
      const rooms::Portal& portal = graph.portals[index];
      return portal.a == rooms::kVacuumRoom || portal.b == rooms::kVacuumRoom;
    });
    listed.push_back({.id = id,
                      .min = {room.bounds.min.x(), room.bounds.min.y(), room.bounds.min.z()},
                      .size = {room.bounds.extent.x(), room.bounds.extent.y(), room.bounds.extent.z()},
                      .volume_cells = room.volume_cells,
                      .portals = static_cast<int>(portals.size()),
                      .open_to_space = leaks});
  }
  reply.Succeed(std::move(listed));
}

void ListVacuumOpenings(flecs::world world, ReplyOf<VacuumOpeningsCommand>& reply) {
  const auto cache = MainGridData<rooms::RoomCache>(world);
  if (!cache || !cache->get().Current()) {
    reply.Succeed({});
    return;
  }
  std::vector<OpeningInfo> listed;
  for (const rooms::Portal& portal : cache->get().Current()->get().portals) {
    if (portal.a != rooms::kVacuumRoom && portal.b != rooms::kVacuumRoom) {
      continue;
    }
    const auto& box = portal.opening;
    listed.push_back({.room = portal.a == rooms::kVacuumRoom ? portal.b : portal.a,
                      .min = {box.min.x(), box.min.y(), box.min.z()},
                      .size = {box.extent.x(), box.extent.y(), box.extent.z()},
                      .area_cells = portal.area_cells,
                      .passable = portal.passable});
  }
  reply.Succeed(std::move(listed));
}

}  // namespace

void RegisterRoomCommandComponents(flecs::world world) {
  z13::flecs_tools::RegisterComponents<RoomsCommand, VacuumOpeningsCommand>(world);
}

void RegisterRoomCommandSystems(flecs::world world) {
  CommandSystem<RoomsCommand>(world, "RoomCommands::List",
                              [](flecs::iter& it, size_t, const RoomsCommand&, ReplyOf<RoomsCommand>& reply) {
                                ListRooms(it.world(), reply);
                              });
  CommandSystem<VacuumOpeningsCommand>(
      world, "RoomCommands::VacuumOpenings",
      [](flecs::iter& it, size_t, const VacuumOpeningsCommand&, ReplyOf<VacuumOpeningsCommand>& reply) {
        ListVacuumOpenings(it.world(), reply);
      });
}

}  // namespace z13::station
