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
#include <unordered_map>
#include <vector>

#include <primitives/cell_box.h>
#include <rooms/room_graph.h>

namespace z13::atmos_module {

// The chambers of the air are the rooms in anchor order.
struct RoomIndex {
  std::vector<z13::station::rooms::RoomId> ids;
  std::unordered_map<z13::station::rooms::RoomId, size_t> chamber_of;
};

RoomIndex IndexOf(const z13::station::rooms::RoomGraph& graph);

// The chamber of `room`, or atmos::kOutside for the vacuum and any room the index lacks.
size_t ChamberOf(const RoomIndex& index, z13::station::rooms::RoomId room);

// The rooms touching the faces of a block, which fills cells of its own, each as often as it touches.
std::vector<size_t> ChambersAround(const z13::station::rooms::RoomGraph& graph, const RoomIndex& index,
                                   const z13::building::primitives::CellBox& block);

}  // namespace z13::atmos_module
