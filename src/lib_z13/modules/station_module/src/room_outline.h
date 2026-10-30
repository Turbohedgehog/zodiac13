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

#include <functional>
#include <optional>
#include <vector>

#include <grid/block_index.h>
#include <primitives/palette.h>
#include <rooms/room_graph.h>
#include <z13/components/block.h>
#include <z13/components/rooms.h>

namespace z13::station {

// What the outline of a room reads the walls from.
struct WallSource {
  std::reference_wrapper<const z13::building::grid::BlockIndex> index;
  std::reference_wrapper<const z13::building::primitives::Palette> palette;
  std::function<std::optional<Block>(z13::building::grid::BlockId)> block_of;
};

// The edges of a room's boundary along oblique walls' own faces, not the stairs of cell boxes.
// Nothing for a room no oblique block touches.
std::optional<std::vector<OverlaySegment>> WallOutline(
    const z13::station::rooms::RoomGraph& graph, z13::station::rooms::RoomId room, const WallSource& source,
    const Rgba& color);

}  // namespace z13::station
