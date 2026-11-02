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

#pragma once

#include <array>
#include <cstddef>
#include <span>
#include <unordered_set>
#include <vector>

#include <flecs.h>

#include <atmos/exchange.h>
#include <rooms/room_graph.h>
#include <z13/components/atmosphere.h>
#include <z13/components/interaction.h>
#include <z13_settings/atmos_tuning.h>
#include <z13_settings/door_tuning.h>

#include "door_at.h"
#include "room_index.h"

// Airlocks (docs/station-systems-plan.md, "Двери"): a controller block in a chamber runs the two doors on the
// chamber's portals. A shut door of a powered airlock opens only by a cycle, and never with the other one open.
namespace z13::atmos_module {

using AirlockQuery = flecs::query<const z13::utility::AirlockCycle, const z13::station::Block>;
using InterlockQuery = flecs::query<const z13::utility::DoorInterlock>;

// The geometry of one airlock in the room graph at hand: the chamber and, for each door, the chamber on its far side.
struct AirlockSide {
  flecs::entity door;
  Eigen::Vector3i cell = Eigen::Vector3i::Zero();
  size_t far_chamber {};
};

struct Airlock {
  flecs::entity controller;
  size_t chamber {};
  std::array<AirlockSide, 2> sides;
};

// The controllers that have exactly two doors on their chamber; the others lose their AirlockDoors.
std::vector<Airlock> FindAirlocks(const AirlockQuery& controllers, flecs::world& world,
                                  const z13::station::rooms::RoomGraph& graph, const RoomIndex& index,
                                  std::span<const DoorAt> doors);

// Every tick: a use of a shut door starts (or redirects) a cycle, and each door of a powered airlock is interlocked
// while it is shut. Returns the doors the airlocks hold, which the pressure interlock leaves alone.
std::unordered_set<flecs::entity_t> TendAirlocks(const AirlockQuery& controllers, const InterlockQuery& interlocks,
                                                 const z13::station::Interactions& interactions,
                                                 flecs::world& world);

// A pump from each cycling airlock's chamber to the side of its target door, to go with the portals' links.
void AddPumpLinks(std::vector<z13::utility::atmos::Link>& links, std::span<const Airlock> airlocks,
                  const z13::AtmosTuning& tuning);

// After a step of the air: a cycle whose chamber is level with its target side, the other door being shut, opens the
// target door and ends.
void FinishCycles(std::span<const Airlock> airlocks, std::span<const z13::utility::atmos::Chamber> chambers,
                  const z13::AtmosTuning& tuning, const z13::DoorTuning& doors);

}  // namespace z13::atmos_module
