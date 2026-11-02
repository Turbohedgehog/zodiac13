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

#include <cstdint>

#include <Eigen/Dense>

// The air of the station's rooms (docs/station-systems-plan.md, "Газ и тепло"), kept by atmos_module.
namespace z13::utility {

// The gases of one room in millimoles and its thermal energy in millijoules, named one by one: state has no array.
// One state entity per room, named by the anchor (atmos_module's room_air_store.h), as state has no vector either.
struct RoomAirEntry {
  using State = void;
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

// Why an automatic door does not open for a body, set on the door's block by AtmosSystem; derived, never state.
enum class InterlockReason : uint8_t { kPressure };

struct DoorInterlock {
  InterlockReason reason {};

  bool operator==(const DoorInterlock&) const = default;
};

}  // namespace z13::utility
