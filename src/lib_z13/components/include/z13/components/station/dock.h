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
#include <vector>

#include <Eigen/Dense>
#include <flecs.h>

#include <z13/components/station/grid.h>

namespace z13::station {

// A cell inside a room of a grid.
struct DockEnd {
  GridId grid = kMainGrid;
  Eigen::Vector3i cell = Eigen::Vector3i::Zero();

  bool operator==(const DockEnd&) const = default;
};

// Joins the room holding one end's cell to the room holding the other's, across grids: a room never leaves its grid
// (docs/multi-grid-plan.md), so a district reaches the main grid through a dock.
struct Dock {
  using State = void;
  DockEnd a;
  DockEnd b;

  bool operator==(const Dock&) const = default;
};

// A room in the graph of its grid; ids are per grid.
struct RoomRef {
  GridId grid = kMainGrid;
  uint32_t room {};

  bool operator==(const RoomRef&) const = default;
};

struct DockLink {
  flecs::entity_t dock {};
  RoomRef a;
  RoomRef b;

  bool operator==(const DockLink&) const = default;
};

// The docks whose two ends lie in rooms, derived each tick from the docks and the rooms of the grids.
struct DockLinks {
  using Singleton = void;
  std::vector<DockLink> list;
};

}  // namespace z13::station
