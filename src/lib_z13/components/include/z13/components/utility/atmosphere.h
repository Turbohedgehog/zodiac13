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
#include <cstdint>

#include <Eigen/Dense>
#include <flecs.h>

#include <z13/components/station/grid_id.h>

// The air of the station's rooms (docs/station-systems-plan.md, "Газ и тепло"), kept by atmos_module.
namespace z13::utility {

// The gases of one room in millimoles and its thermal energy in millijoules, named one by one: state has no array.
// One state entity per room, named by the grid and the anchor (atmos_module's room_air_store.h), as state has no
// vector either.
struct RoomAirEntry {
  using State = void;
  z13::station::GridId grid = z13::station::kMainGrid;
  Eigen::Vector3i anchor = Eigen::Vector3i::Zero();
  int64_t oxygen {};
  int64_t nitrogen {};
  int64_t carbon_dioxide {};
  int64_t energy {};

  bool operator==(const RoomAirEntry&) const = default;
};

// On a grid's entity. Room ids are ephemeral, so the air is kept by the room's anchor, and `fingerprint` names the room
// graph the anchors belong to: on a switch the air is split and merged by the cells the old and the new rooms share.
struct RoomAirBasis {
  using State = void;
  uint64_t fingerprint {};
  // False until the first rooms appear, which start with standard air.
  bool seeded {};

  bool operator==(const RoomAirBasis&) const = default;
};

// On the block of an airlock controller, added when it appears.
struct AirlockCycle {
  using State = void;
  // Whether the chamber is being brought to the pressure of the far side of the door at `door_cell`, which then opens.
  bool cycling {};
  Eigen::Vector3i door_cell = Eigen::Vector3i::Zero();

  bool operator==(const AirlockCycle&) const = default;
};

struct AirlockDoor {
  flecs::entity_t entity {};
  Eigen::Vector3i cell = Eigen::Vector3i::Zero();
};

// The two doors on the chamber of an airlock controller, ordered by cell; set by AtmosSystem while the rooms have
// exactly two, never state.
struct AirlockDoors {
  std::array<AirlockDoor, 2> doors;
};

}  // namespace z13::utility
