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

#include <z13_rooms/room_graph.h>

namespace z13::station::rooms {

// The cells a room of one graph shares with a room of another.
struct RoomOverlap {
  RoomIndex before {};
  RoomIndex after {};
  int64_t cells {};

  bool operator==(const RoomOverlap&) const = default;
};

// Every pair of rooms sharing cells, sorted by (before, after): what a rebuild did to
// each room, from which a room's state is split or merged into the new rooms by volume.
// Cells outside a graph's grid count as its vacuum, so a room built where there was none
// overlaps the vacuum. Sealed cells overlap nothing.
std::vector<RoomOverlap> OverlapsBetween(const RoomGraph& before, const RoomGraph& after);

}  // namespace z13::station::rooms
