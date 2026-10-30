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

#include <rooms/cut_walls.h>

#include <algorithm>
#include <unordered_set>

#include <rooms/room_graph.h>

namespace z13::station::rooms {

using z13::building::primitives::CellBox;
using z13::building::primitives::CellHash;

std::vector<CellBox> CutWalls(std::span<const Eigen::Vector3i> sectors, int sector_cells, const CellBox& within) {
  const std::unordered_set<Eigen::Vector3i, CellHash> held(sectors.begin(), sectors.end());
  std::vector<CellBox> walls;
  for (const Eigen::Vector3i& sector : sectors) {
    for (int axis = 0; axis < Eigen::Vector3i::RowsAtCompileTime; ++axis) {
      for (const int side : {-1, 1}) {
        if (held.contains(sector + side * Eigen::Vector3i::Unit(axis))) {
          continue;
        }
        CellBox wall {.min = sector * sector_cells, .extent = Eigen::Vector3i::Constant(sector_cells)};
        wall.extent[axis] = 1;
        wall.min[axis] = side > 0 ? (sector[axis] + 1) * sector_cells : sector[axis] * sector_cells - 1;
        if (const CellBox inside = wall.Intersection(within); (inside.extent.array() > 0).all()) {
          walls.push_back(inside);
        }
      }
    }
  }
  // Walls cut to `within` may start at one cell: the extent breaks the tie.
  std::ranges::sort(walls, [](const CellBox& a, const CellBox& b) {
    return a.min != b.min ? AnchorBefore(a.min, b.min) : AnchorBefore(a.extent, b.extent);
  });
  return walls;
}

}  // namespace z13::station::rooms
