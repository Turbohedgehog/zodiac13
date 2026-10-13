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
#include <memory>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

#include <Eigen/Dense>

#include <building_tuning_generated.h>
#include <primitives/placement.h>
#include <rooms/free_space.h>
#include <rooms/portal_source.h>
#include <rooms/room_id.h>
#include <rooms/room_sources.h>

namespace z13::station::rooms {

// Orders cells by (z, y, x), as anchors are compared.
bool AnchorBefore(const Eigen::Vector3i& first, const Eigen::Vector3i& second);

// Strict orders of the sources, so two source lists compare by a merge.
bool SealedBefore(const z13::building::primitives::CellBox& first, const z13::building::primitives::CellBox& second);
bool PortalSourceBefore(const PortalSource& first, const PortalSource& second);

// A connected free volume, keyed by RoomId. `anchor` is its lowest cell by (z, y, x), which
// orders rooms and picks which one keeps its id when rooms merge or split (UpdateRooms).
struct Room {
  Eigen::Vector3i anchor = Eigen::Vector3i::Zero();
  int64_t volume_cells {};
  z13::building::primitives::CellBox bounds;
  // The sum of its cells' coordinates: how the leaves cut it doesn't change it, while a
  // block moved inside the room does, though volume and bounds stay.
  Eigen::Vector3<int64_t> cell_sum = Eigen::Vector3<int64_t>::Zero();

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

// The "building" settings rooms are built by.
struct RoomSettings {
  int chunk_cells = fbs::building::BuildingTuningT {}.room_chunk_cells;
  int max_opening_cells = fbs::building::BuildingTuningT {}.room_max_opening_cells;

  bool operator==(const RoomSettings&) const = default;
};

// Portals are sorted by where their opening is. Room ids carry no order: whatever depends
// on the order of rooms goes by ByAnchor().
struct RoomGraph {
  std::unordered_map<RoomId, Room> rooms;
  std::vector<Portal> portals;
  // Per room, the indices of the portals it is a side of.
  std::unordered_map<RoomId, std::vector<size_t>> room_portals;
  FreeSpace space;
  // Per chunk of `space`, the room of each of its leaves; kept apart from the leaves so a
  // rebuild that only renames rooms shares the chunks.
  std::unordered_map<Eigen::Vector3i, std::shared_ptr<const std::vector<RoomId>>, z13::building::primitives::CellHash>
      leaf_rooms;
  // The first id no room of this graph or of the graphs it was built from has had.
  RoomId next_id {kVacuumRoom + 1};
  // What the graph was built from, sorted by SealedBefore and PortalSourceBefore: the next
  // update compares the new sources with these.
  RoomSources sources;
  // The holes found as doorways (FindOpenings), sorted by PortalSourceBefore: the free space
  // holds them as sealed, and each is a portal.
  std::vector<PortalSource> openings;
  RoomSettings settings;

  // Nothing for a sealed cell; the vacuum outside the free space's area.
  std::optional<RoomId> RoomAt(const Eigen::Vector3i& cell) const;
  RoomId RoomOf(const LeafRef& leaf) const { return (*leaf_rooms.at(leaf.chunk))[leaf.leaf]; }
  // Nothing for an id the graph doesn't know, e.g. one kept from another graph.
  std::optional<std::reference_wrapper<const Room>> FindRoom(RoomId room) const;
  // The ids of every room but the vacuum, by anchor.
  std::vector<RoomId> ByAnchor() const;
  // Empty for a room without portals.
  std::span<const size_t> PortalsOf(RoomId room) const;
};

using OptionalRoomGraph = std::optional<std::reference_wrapper<const RoomGraph>>;

}  // namespace z13::station::rooms
