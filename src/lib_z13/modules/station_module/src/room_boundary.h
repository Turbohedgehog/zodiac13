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
#include <unordered_map>
#include <utility>
#include <vector>

#include <grid/block_index.h>
#include <rooms/room_graph.h>

#include "room_outline.h"
#include "wall_faces.h"

namespace z13::station::outline {

// What a room's boundary is read from: the blocks next to it, their faces made once each.
class RoomBoundary {
 public:
  RoomBoundary(const z13::station::rooms::RoomGraph& graph, z13::station::rooms::RoomId room,
               const WallSource& source)
      : graph_(graph), room_(room), source_(source) {}

  bool InRoom(const Cell& cell) const { return graph_.get().RoomAt(cell) == room_; }

  std::optional<std::pair<z13::building::grid::BlockId, std::reference_wrapper<const BlockFaces>>> ObliqueAt(
      const Cell& cell);

  const BlockFaces& FacesOfBlock(z13::building::grid::BlockId id) const { return faces_.at(id); }

  std::vector<std::reference_wrapper<const BlockFaces>> ObliqueAround(const Cell& cell);

 private:
  std::reference_wrapper<const z13::station::rooms::RoomGraph> graph_;
  z13::station::rooms::RoomId room_ {};
  std::reference_wrapper<const WallSource> source_;
  std::unordered_map<z13::building::grid::BlockId, BlockFaces> faces_;
};

}  // namespace z13::station::outline
