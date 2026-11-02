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

#include "airlock.h"

#include <algorithm>
#include <cstdlib>
#include <optional>
#include <utility>

#include <lib_core/utils/flecs_utils.h>

#include <primitives/placement.h>
#include <z13/components/block.h>
#include <z13/components/door.h>
#include <z13/components/utilities.h>

namespace z13::atmos_module {

namespace {

using z13::station::Block;
using z13::station::DoorState;
using z13::station::Interaction;
using z13::station::rooms::Portal;
using z13::station::rooms::RoomGraph;
using z13::station::rooms::RoomId;
using z13::utility::AirlockCycle;
using z13::utility::AirlockDoor;
using z13::utility::AirlockDoors;
using z13::utility::DoorInterlock;
using z13::utility::InterlockReason;
namespace atmos = z13::utility::atmos;
using Methods = z13::fbs::station::InteractionMethods;

bool IsShut(flecs::entity door) {
  const auto* state = door.try_get<DoorState>();
  return state != nullptr && state->position == 0 && !state->open_wanted;
}

void Shut(flecs::entity door) {
  auto& state = door.get_mut<DoorState>();
  if (state.open_wanted) {
    state.open_wanted = false;
    state.hold_ticks = 0;
    door.modified<DoorState>();
  }
}

bool WasUsed(const z13::station::Interactions& interactions, flecs::entity_t door) {
  return std::ranges::any_of(interactions.list, [door](const Interaction& interaction) {
    return interaction.target == door && interaction.method == Methods::Use;
  });
}

std::optional<size_t> SideAt(const Airlock& airlock, const Eigen::Vector3i& cell) {
  const auto found = std::ranges::find(airlock.sides, cell, &AirlockSide::cell);
  if (found == airlock.sides.end()) {
    return std::nullopt;
  }
  return static_cast<size_t>(found - airlock.sides.begin());
}

// The side an airlock is cycling to, if its controller is powered and the other door is shut.
std::optional<size_t> CyclingSide(const Airlock& airlock) {
  if (!airlock.controller.is_alive() || !z13::utility::IsSupplied(airlock.controller)) {
    return std::nullopt;
  }
  const auto* cycle = airlock.controller.try_get<AirlockCycle>();
  const auto side = cycle != nullptr && cycle->cycling ? SideAt(airlock, cycle->door_cell) : std::nullopt;
  if (!side || !IsShut(airlock.sides[1 - *side].door)) {
    return std::nullopt;
  }
  return side;
}

std::optional<Airlock> AirlockOf(flecs::entity controller, const Block& block, const RoomGraph& graph,
                                 const RoomIndex& index, std::span<const DoorAt> doors) {
  std::vector<size_t> around = ChambersAround(graph, index, z13::building::primitives::OccupiedCells(block));
  std::ranges::sort(around);
  around.erase(std::ranges::unique(around).begin(), around.end());
  if (around.size() != 1) {
    return std::nullopt;
  }
  const RoomId room = index.ids[around.front()];
  std::vector<AirlockSide> sides;
  for (const Portal& portal : graph.portals) {
    if (portal.a != room && portal.b != room) {
      continue;
    }
    const auto door = DoorAtOpening(doors, portal.opening);
    if (door && !std::ranges::contains(sides, door->get().entity, &AirlockSide::door)) {
      sides.push_back({.door = door->get().entity,
                       .cell = door->get().entity.get<Block>().cell,
                       .far_chamber = ChamberOf(index, portal.a == room ? portal.b : portal.a)});
    }
  }
  if (sides.size() != 2) {
    return std::nullopt;
  }
  std::ranges::sort(sides, z13::station::rooms::AnchorBefore, &AirlockSide::cell);
  return Airlock {.controller = controller, .chamber = around.front(), .sides = {sides[0], sides[1]}};
}

AirlockDoors DoorsOfAirlock(const Airlock& airlock) {
  const auto& [first, second] = airlock.sides;
  return {.doors = {AirlockDoor {.entity = first.door.id(), .cell = first.cell},
                    AirlockDoor {.entity = second.door.id(), .cell = second.cell}}};
}

}  // namespace

std::vector<Airlock> FindAirlocks(const AirlockQuery& controllers, flecs::world& world, const RoomGraph& graph,
                                  const RoomIndex& index, std::span<const DoorAt> doors) {
  std::vector<Airlock> found;
  std::vector<flecs::entity> without;
  controllers.each([&](flecs::entity controller, const AirlockCycle&, const Block& block) {
    if (auto airlock = AirlockOf(controller, block, graph, index, doors)) {
      found.push_back(std::move(*airlock));
    } else {
      without.push_back(controller);
    }
  });
  const z13::ImmediateScope immediate(world);
  for (const Airlock& airlock : found) {
    airlock.controller.set(DoorsOfAirlock(airlock));
  }
  for (flecs::entity controller : without) {
    controller.remove<AirlockDoors>();
  }
  return found;
}

std::unordered_set<flecs::entity_t> TendAirlocks(const AirlockQuery& controllers, const InterlockQuery& interlocks,
                                                 const z13::station::Interactions& interactions,
                                                 flecs::world& world) {
  const z13::ImmediateScope immediate(world);
  std::unordered_set<flecs::entity_t> held;
  std::vector<std::pair<flecs::entity, AirlockCycle>> cycles;
  controllers.each([&](flecs::entity controller, const AirlockCycle& stored, const Block&) {
    const auto* airlock = controller.try_get<AirlockDoors>();
    if (airlock == nullptr || !world.is_alive(airlock->doors[0].entity) || !world.is_alive(airlock->doors[1].entity)) {
      return;
    }
    if (!z13::utility::IsSupplied(controller)) {
      cycles.emplace_back(controller, AirlockCycle {});
      return;
    }
    AirlockCycle cycle = stored;
    for (const AirlockDoor& door : airlock->doors) {
      if (WasUsed(interactions, door.entity) && IsShut(world.entity(door.entity))) {
        cycle = {.cycling = true, .door_cell = door.cell};
      }
    }
    cycles.emplace_back(controller, cycle);
    for (const AirlockDoor& door : airlock->doors) {
      const flecs::entity entity = world.entity(door.entity);
      held.insert(door.entity);
      if (cycle.cycling && door.cell != cycle.door_cell) {
        Shut(entity);
      }
      const bool shut = IsShut(entity);
      const auto* interlock = entity.try_get<DoorInterlock>();
      if (shut && (interlock == nullptr || interlock->reason != InterlockReason::kAirlock)) {
        entity.set(DoorInterlock {.reason = InterlockReason::kAirlock});
      } else if (!shut && interlock != nullptr) {
        entity.remove<DoorInterlock>();
      }
    }
  });
  for (const auto& [controller, cycle] : cycles) {
    if (controller.get<AirlockCycle>() != cycle) {
      controller.set(cycle);
    }
  }
  std::vector<flecs::entity> released;
  interlocks.each([&](flecs::entity door, const DoorInterlock& interlock) {
    if (interlock.reason == InterlockReason::kAirlock && !held.contains(door.id())) {
      released.push_back(door);
    }
  });
  std::ranges::for_each(released, [](flecs::entity door) { door.remove<DoorInterlock>(); });
  return held;
}

void AddPumpLinks(std::vector<atmos::Link>& links, std::span<const Airlock> airlocks,
                  const z13::AtmosTuning& tuning) {
  for (const Airlock& airlock : airlocks) {
    if (const auto side = CyclingSide(airlock)) {
      links.push_back({.a = airlock.chamber,
                       .b = airlock.sides[*side].far_chamber,
                       .area_cells = tuning.airlock_pump_cells});
    }
  }
}

void FinishCycles(std::span<const Airlock> airlocks, std::span<const atmos::Chamber> chambers,
                  const z13::AtmosTuning& tuning, const z13::DoorTuning& doors) {
  for (const Airlock& airlock : airlocks) {
    const auto side = CyclingSide(airlock);
    if (!side) {
      continue;
    }
    const AirlockSide& target = airlock.sides[*side];
    const int64_t difference =
        atmos::PressureOf(chambers, airlock.chamber) - atmos::PressureOf(chambers, target.far_chamber);
    if (std::abs(difference) > tuning.interlock_pascals) {
      continue;
    }
    auto& state = target.door.get_mut<DoorState>();
    state.open_wanted = true;
    state.hold_ticks = static_cast<int>(doors.close_delay_ticks);
    target.door.modified<DoorState>();
    airlock.controller.set(AirlockCycle {});
  }
}

}  // namespace z13::atmos_module
