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

#include <z13_rooms/room_overlap.h>

#include <algorithm>
#include <array>
#include <limits>
#include <map>
#include <span>
#include <utility>

namespace z13::station::rooms {

namespace {

// A column of `graph` over the z range [low, high): its free intervals, with the vacuum
// above, below and outside its grid.
std::vector<FreeInterval> ColumnOver(const RoomGraph& graph, const Eigen::Vector2i& xy, int low, int high) {
  const ColumnMap& columns = graph.columns;
  const Eigen::Vector2i local = xy - columns.Origin();
  const bool inside = (local.array() >= 0).all() && (local.array() < columns.Size().array()).all();
  if (!inside) {
    return {{.begin = low, .end = high, .room = kVacuumRoom}};
  }
  std::vector<FreeInterval> column;
  if (low < columns.Heights().x()) {
    column.push_back({.begin = low, .end = columns.Heights().x(), .room = kVacuumRoom});
  }
  const std::span<const FreeInterval> own = columns.Column(xy);
  column.insert(column.end(), own.begin(), own.end());
  if (columns.Heights().y() < high) {
    column.push_back({.begin = columns.Heights().y(), .end = high, .room = kVacuumRoom});
  }
  return column;
}

}  // namespace

std::vector<RoomOverlap> OverlapsBetween(const RoomGraph& before, const RoomGraph& after) {
  // The grids of both graphs, which all else is vacuum to; one without blocks has none.
  const std::array<const ColumnMap*, 2> grids {&before.columns, &after.columns};
  Eigen::Vector2i low = Eigen::Vector2i::Constant(std::numeric_limits<int>::max());
  Eigen::Vector2i high = Eigen::Vector2i::Constant(std::numeric_limits<int>::min());
  int z_low = std::numeric_limits<int>::max();
  int z_high = std::numeric_limits<int>::min();
  for (const ColumnMap* grid : grids) {
    if (grid->ColumnCount() == 0) {
      continue;
    }
    low = low.cwiseMin(grid->Origin());
    high = high.cwiseMax(grid->Origin() + grid->Size());
    z_low = std::min(z_low, grid->Heights().x());
    z_high = std::max(z_high, grid->Heights().y());
  }
  std::map<std::pair<RoomIndex, RoomIndex>, int64_t> cells;
  for (int y = low.y(); y < high.y(); ++y) {
    for (int x = low.x(); x < high.x(); ++x) {
      const std::vector<FreeInterval> before_column = ColumnOver(before, {x, y}, z_low, z_high);
      const std::vector<FreeInterval> after_column = ColumnOver(after, {x, y}, z_low, z_high);
      size_t i {};
      size_t j {};
      while (i < before_column.size() && j < after_column.size()) {
        const int overlap = std::min(before_column[i].end, after_column[j].end) -
                            std::max(before_column[i].begin, after_column[j].begin);
        if (overlap > 0) {
          cells[{before_column[i].room, after_column[j].room}] += overlap;
        }
        if (before_column[i].end < after_column[j].end) {
          ++i;
        } else {
          ++j;
        }
      }
    }
  }
  std::vector<RoomOverlap> overlaps;
  for (const auto& [pair, count] : cells) {
    overlaps.push_back({.before = pair.first, .after = pair.second, .cells = count});
  }
  return overlaps;
}

}  // namespace z13::station::rooms
