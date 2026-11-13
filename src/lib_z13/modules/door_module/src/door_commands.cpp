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


#include "door_commands.h"

#include <algorithm>
#include <format>
#include <optional>
#include <string>
#include <vector>

#include <Eigen/Dense>
#include <rfl/enums.hpp>

#include <lib_core/commands/command_registry.h>
#include <lib_core/state/world_state.h>
#include <z13/components/station/block.h>
#include <z13/components/station/door.h>
#include <z13/components/station/grid.h>
#include <z13/components/station/station_mode.h>
#include <grid/block_index.h>

namespace z13::door_module {

namespace {

using z13::station::DoorMode;
using z13::building::grid::BlockIndex;
using z13::station::DoorState;

struct CellInfo {
  int x {};
  int y {};
  int z {};
};

struct DoorInfo {
  std::string name;
  CellInfo cell;
  std::string mode;
  double open_share {};  // 0 shut, 1 fully open
  bool open_wanted {};
  std::string interlock;  // empty when nothing holds the door shut
};

struct DoorsCommand {
  using Command = z13::Command<std::vector<DoorInfo>,
                               "Lists the main grid's doors: name, cell, mode, how far open (0..1), whether it is "
                               "asked to open and what interlock holds it, if any.",
                               Tags<kDebugTag>>;
  int unused {};
};

struct SetDoorCommand {
  using Command = z13::Command<
      std::string,
      "Sets the door at cell (x, y, z) (any cell of its block): `mode` is an enumerator of z13::station::DoorMode "
      "(kAuto, kManual, kLocked, kWelded; empty keeps it), `open` asks it open (true) or shut (false). A locked or "
      "welded door stays shut, an automatic one follows its sensor: to hold one open, make it kManual. Station mode "
      "only. Answers the door's name.",
      Tags<kDebugTag, kBuildTag>>;
  int x {};
  int y {};
  int z {};
  std::string mode;
  std::optional<bool> open;
};

std::expected<flecs::entity, std::string> MainGrid(flecs::world world) {
  if (!world.has<z13::station::StationMode>()) {
    return std::unexpected(std::string {"not in station mode: start a station scene first"});
  }
  const flecs::entity main_grid = z13::station::MainGridEntity(world);
  if (!main_grid.is_valid() || !main_grid.has<BlockIndex>()) {
    return std::unexpected(std::string {"the station has no main grid yet"});
  }
  return main_grid;
}

void ListDoors(flecs::world world, ReplyOf<DoorsCommand>& reply) {
  std::vector<DoorInfo> listed;
  world.query<const z13::station::Block, const DoorState>().each(
      [&listed, &world](flecs::entity entity, const z13::station::Block& block, const DoorState& state) {
        if (block.grid != z13::station::kMainGrid) {
          return;
        }
        listed.push_back({.name = std::string(entity.name().c_str()),
                          .cell = {block.cell.x(), block.cell.y(), block.cell.z()},
                          .mode = std::string(rfl::enum_to_string(state.mode)),
                          .open_share = static_cast<double>(state.position) / z13::station::kDoorOpen,
                          .open_wanted = state.open_wanted,
                          .interlock = entity.has<z13::station::DoorInterlock>()
                                           ? std::string(rfl::enum_to_string(entity.get<z13::station::DoorInterlock>().reason))
                                           : std::string {}});
      });
  std::ranges::sort(listed, {}, &DoorInfo::name);
  reply.Succeed(std::move(listed));
}

void SetDoor(flecs::world world, const SetDoorCommand& request, ReplyOf<SetDoorCommand>& reply) {
  const auto grid = MainGrid(world);
  if (!grid) {
    reply.Fail(grid.error());
    return;
  }
  const auto found = grid->get<BlockIndex>().At({request.x, request.y, request.z});
  if (!found || !world.entity(*found).has<DoorState>()) {
    reply.Fail("no door in that cell: build one with `build_block` and wait a frame");
    return;
  }
  std::optional<DoorMode> mode;
  if (!request.mode.empty()) {
    const auto parsed = rfl::string_to_enum<DoorMode>(request.mode);
    if (!parsed) {
      reply.Fail(std::format("unknown door mode '{}'", request.mode));
      return;
    }
    mode = *parsed;
  }
  const flecs::entity door = world.entity(*found);
  DoorState state = door.get<DoorState>();
  state.mode = mode.value_or(state.mode);
  state.open_wanted = request.open.value_or(state.open_wanted);
  door.set(state);
  reply.Succeed(std::string(door.name().c_str()));
}

}  // namespace

void RegisterDoorCommandComponents(flecs::world world) {
  z13::flecs_tools::RegisterComponents<DoorsCommand, SetDoorCommand>(world);
}

void RegisterDoorCommandSystems(flecs::world world) {
  CommandSystem<DoorsCommand>(world, "DoorCommands::List",
                              [](flecs::iter& it, size_t, const DoorsCommand&, ReplyOf<DoorsCommand>& reply) {
                                ListDoors(it.world(), reply);
                              });
  CommandSystem<SetDoorCommand>(
      world, "DoorCommands::Set", [](auto& builder) { builder.immediate(); },
      [](flecs::iter& it, size_t, const SetDoorCommand& request, ReplyOf<SetDoorCommand>& reply) {
        SetDoor(it.world(), request, reply);
      });
}

}  // namespace z13::door_module
