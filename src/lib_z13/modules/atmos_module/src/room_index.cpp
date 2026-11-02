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

#include "room_index.h"

#include <atmos/exchange.h>

namespace z13::atmos_module {

namespace {

using z13::building::primitives::CellBox;
using z13::station::rooms::RoomGraph;
using z13::station::rooms::RoomId;
namespace atmos = z13::utility::atmos;

// A cell sharing a face with a block lies outside its box along exactly one axis.
constexpr Eigen::Index kFaceNeighbour = 1;

}  // namespace

RoomIndex IndexOf(const RoomGraph& graph) {
  RoomIndex index {.ids = graph.ByAnchor()};
  for (size_t i = 0; i < index.ids.size(); ++i) {
    index.chamber_of.emplace(index.ids[i], i);
  }
  return index;
}

size_t ChamberOf(const RoomIndex& index, RoomId room) {
  const auto found = index.chamber_of.find(room);
  return found != index.chamber_of.end() ? found->second : atmos::kOutside;
}

std::vector<size_t> ChambersAround(const RoomGraph& graph, const RoomIndex& index, const CellBox& block) {
  std::vector<size_t> chambers;
  const CellBox around = block.Grown(Eigen::Vector3i::Ones());
  for (int x = around.min.x(); x < around.End().x(); ++x) {
    for (int y = around.min.y(); y < around.End().y(); ++y) {
      for (int z = around.min.z(); z < around.End().z(); ++z) {
        const Eigen::Vector3i cell(x, y, z);
        const Eigen::Index outside =
            ((cell.array() < block.min.array()) || (cell.array() >= block.End().array())).count();
        if (outside != kFaceNeighbour) {
          continue;
        }
        const auto room = graph.RoomAt(cell);
        const size_t chamber = room ? ChamberOf(index, *room) : atmos::kOutside;
        if (chamber != atmos::kOutside) {
          chambers.push_back(chamber);
        }
      }
    }
  }
  return chambers;
}

}  // namespace z13::atmos_module
