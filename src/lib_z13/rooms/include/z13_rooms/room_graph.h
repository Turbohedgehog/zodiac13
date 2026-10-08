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
#include <optional>
#include <vector>

#include <Eigen/Dense>

#include <z13_primitives/placement.h>
#include <z13_rooms/column_map.h>

namespace z13::station::rooms {

// A connected free volume. Its identity is `anchor`, the lowest cell by (z, y, x): it
// survives a rebuild that doesn't change the room's lowest cell.
struct Room {
  Eigen::Vector3i anchor = Eigen::Vector3i::Zero();
  int64_t volume_cells {};
  z13::building::primitives::CellBox bounds;

  bool operator==(const Room&) const = default;
};

// An opening between two rooms through a door or window block.
struct Portal {
  RoomIndex a {};
  RoomIndex b {};
  int area_cells {};
  bool visible {};
  bool passable {};
  // The cells the opening spans, through the block.
  z13::building::primitives::CellBox opening;

  bool operator==(const Portal&) const = default;
};

// Rooms sorted by anchor after the vacuum (room 0), portals sorted by where their opening is.
struct RoomGraph {
  std::vector<Room> rooms;
  std::vector<Portal> portals;
  ColumnMap columns;

  std::optional<RoomIndex> RoomAt(const Eigen::Vector3i& cell) const { return columns.RoomAt(cell); }
};

}  // namespace z13::station::rooms
