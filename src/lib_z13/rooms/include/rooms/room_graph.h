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
#include <functional>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

#include <Eigen/Dense>

#include <primitives/placement.h>
#include <rooms/column_map.h>
#include <rooms/portal_source.h>

namespace z13::station::rooms {

// Orders cells by (z, y, x), as anchors are compared.
bool AnchorBefore(const Eigen::Vector3i& first, const Eigen::Vector3i& second);

// A connected free volume. Its identity is `anchor`, the lowest cell by (z, y, x): it
// survives a rebuild that doesn't change the room's lowest cell.
struct Room {
  Eigen::Vector3i anchor = Eigen::Vector3i::Zero();
  int64_t volume_cells {};
  z13::building::primitives::CellBox bounds;

  bool operator==(const Room&) const = default;
};

struct Portal {
  RoomId a {};
  RoomId b {};
  int area_cells {};
  bool visible {};
  bool passable {};
  z13::building::primitives::CellBox opening;
  // The axis the opening is crossed along, and which of a and b lies on its low side.
  Axis axis {};
  RoomId low_side {};

  bool operator==(const Portal&) const = default;
};

// Portals are sorted by where their opening is. Room ids carry no order: whatever depends
// on the order of rooms goes by ByAnchor().
struct RoomGraph {
  std::unordered_map<RoomId, Room> rooms;
  std::vector<Portal> portals;
  // Per room, the indices of the portals it is a side of.
  std::unordered_map<RoomId, std::vector<size_t>> room_portals;
  ColumnMap columns;
  // The first id no room of this graph or of the graphs it was built from has had.
  RoomId next_id {kVacuumRoom + 1};

  std::optional<RoomId> RoomAt(const Eigen::Vector3i& cell) const { return columns.RoomAt(cell); }
  // The ids of every room but the vacuum, by anchor.
  std::vector<RoomId> ByAnchor() const;
  // Empty for a room without portals.
  std::span<const size_t> PortalsOf(RoomId room) const;

  bool operator==(const RoomGraph&) const = default;
};

using OptionalRoomGraph = std::optional<std::reference_wrapper<const RoomGraph>>;

}  // namespace z13::station::rooms
