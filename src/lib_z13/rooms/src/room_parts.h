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
#include <unordered_map>
#include <utility>
#include <vector>

#include <Eigen/Dense>

#include <primitives/placement.h>
#include <rooms/portal_source.h>
#include <rooms/room_graph.h>
#include <rooms/room_id.h>

namespace z13::station::rooms {

// A room's extent while its leaves are gathered.
struct RoomExtent {
  Eigen::Vector3i anchor = Eigen::Vector3i::Zero();
  Eigen::Vector3i low = Eigen::Vector3i::Zero();
  Eigen::Vector3i high = Eigen::Vector3i::Zero();
  int64_t volume {};
  Eigen::Vector3<int64_t> cell_sum = Eigen::Vector3<int64_t>::Zero();
};

// A leaf's part of one side of an opening, on the opening's face: the face's two other
// axes, in order.
struct FacePatch {
  Eigen::Vector2i min = Eigen::Vector2i::Zero();
  Eigen::Vector2i end = Eigen::Vector2i::Zero();
  RoomId room {};
};

// The cells of one opening's face between a pair of rooms.
struct OpeningShare {
  // Lower id first.
  std::pair<RoomId, RoomId> rooms;
  int area_cells {};
  RoomId low_side {};
};

RoomExtent ExtentOf(const z13::building::primitives::CellBox& leaf);
void Absorb(RoomExtent& room, const z13::building::primitives::CellBox& leaf);
Room ToRoom(const RoomExtent& extent);

// The portals of one opening, a portal per pair of rooms its face joins, by pair.
std::vector<Portal> PortalsThrough(const RoomGraph& graph, const PortalSource& source);
// Portals in the graph's order: by opening, then by pair.
bool PortalBefore(const Portal& first, const Portal& second);
std::unordered_map<RoomId, std::vector<size_t>> PortalsByRoom(const std::vector<Portal>& portals);

}  // namespace z13::station::rooms
