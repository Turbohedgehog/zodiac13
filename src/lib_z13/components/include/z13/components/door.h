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
#include <limits>
#include <vector>

#include <Eigen/Dense>

// Doors of the station (docs/station-systems-plan.md, "Двери"): station_module moves them, bullet_module lets bodies
// through an open one, the renderers draw the leaf where it stands.
namespace z13::station {

enum class DoorMode : uint8_t {
  // Opens for a body in its sensor while powered.
  kAuto,
  // Opens and closes only by being used.
  kManual,
  // Stays shut.
  kLocked,
  // Stays shut for good until cut open.
  kWelded,
};

// How far open a door's leaf is, from shut (0) to fully open (kDoorOpen): the whole range of the type, whole numbers
// all through so peers agree.
using DoorPosition = uint16_t;
inline constexpr int kDoorOpen = std::numeric_limits<DoorPosition>::max();

// On the block of a door primitive, added when the block appears; a door is shut and automatic at first.
struct DoorState {
  using State = void;
  DoorMode mode {};
  DoorPosition position {};
  bool open_wanted {};
  // Ticks an automatic door keeps open after the last body left its sensor.
  int hold_ticks {};
  // Not open enough yet for a body to pass; set from the position by DoorSystem for the physics to read.
  bool leaf_blocks = true;

  bool operator==(const DoorState&) const = default;
};

// The cells of an opening a door has opened at least a little, for the renderer to see through; derived, never state.
struct DoorOpening {
  Eigen::Vector3i min = Eigen::Vector3i::Zero();
  Eigen::Vector3i extent = Eigen::Vector3i::Zero();

  bool operator==(const DoorOpening&) const = default;
};

struct DoorPortals {
  using Singleton = void;
  std::vector<DoorOpening> open;
};

}  // namespace z13::station
