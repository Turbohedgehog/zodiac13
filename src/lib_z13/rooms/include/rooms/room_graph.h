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

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <Eigen/Dense>

#include <primitives/placement.h>
#include <rooms/column_map.h>
#include <rooms/portal_source.h>

namespace z13::station::rooms {

// A connected free volume. Its identity is `anchor`, the lowest cell by (z, y, x): it
// survives a rebuild that doesn't change the room's lowest cell.
struct Room {
  Eigen::Vector3i anchor = Eigen::Vector3i::Zero();
  int64_t volume_cells {};
  z13::building::primitives::CellBox bounds;

  bool operator==(const Room&) const = default;
};

struct Portal {
  RoomIndex a {};
  RoomIndex b {};
  int area_cells {};
  bool visible {};
  bool passable {};
  z13::building::primitives::CellBox opening;
  // The axis the opening is crossed along, and which of a and b lies on its low side.
  Axis axis {};
  RoomIndex low_side {};

  bool operator==(const Portal&) const = default;
};

// Rooms sorted by anchor after the vacuum (room 0), portals sorted by where their opening is.
struct RoomGraph {
  std::vector<Room> rooms;
  std::vector<Portal> portals;
  // Per room, the indices of the portals it is a side of.
  std::vector<std::vector<size_t>> room_portals;
  ColumnMap columns;

  std::optional<RoomIndex> RoomAt(const Eigen::Vector3i& cell) const { return columns.RoomAt(cell); }
};

}  // namespace z13::station::rooms
