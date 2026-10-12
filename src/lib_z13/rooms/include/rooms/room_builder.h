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

#include <building_tuning_generated.h>
#include <primitives/placement.h>
#include <rooms/portal_source.h>
#include <rooms/room_graph.h>

namespace z13::station::rooms {

struct RoomSources {
  std::vector<z13::building::primitives::CellBox> sealed;
  std::vector<PortalSource> portals;
};

// Splits the free space into rooms, the connected sets of its leaves (FreeSpace). Free
// space that reaches the edge of the station's bounds, or has no floor or roof, is the
// vacuum. Rooms get ids from `first_id` on, by anchor.
std::expected<RoomGraph, std::string> BuildRooms(
    const RoomSources& sources, RoomId first_id = kVacuumRoom + 1,
    int chunk_cells = fbs::building::BuildingTuningT {}.room_chunk_cells);

}  // namespace z13::station::rooms
