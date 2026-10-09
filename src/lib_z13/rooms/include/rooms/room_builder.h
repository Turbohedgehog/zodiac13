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
#include <expected>
#include <string>
#include <vector>

#include <primitives/placement.h>
#include <rooms/portal_source.h>
#include <rooms/room_graph.h>

namespace z13::station::rooms {

// The most columns (x, y) the grid spanning the station may have, which keeps a stray
// block far from the rest from allocating the whole plane in between.
inline constexpr size_t kMaxColumns = 16u * 1024u * 1024u;

struct RoomSources {
  std::vector<z13::building::primitives::CellBox> sealed;
  std::vector<PortalSource> portals;
};

// Splits the free space into rooms by flood fill over column intervals. Free space that
// reaches the edge of the station's bounds, or has no floor or roof, is the vacuum.
std::expected<RoomGraph, std::string> BuildRooms(const RoomSources& sources);

}  // namespace z13::station::rooms
