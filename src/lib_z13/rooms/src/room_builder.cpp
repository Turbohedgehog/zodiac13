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

#include <rooms/room_builder.h>

#include <algorithm>
#include <array>
#include <format>
#include <limits>
#include <map>
#include <iterator>
#include <numeric>
#include <tuple>
#include <utility>

#include <rooms/disjoint_sets.h>

#include "room_builder_types.h"

namespace z13::station::rooms {

namespace {

using z13::building::primitives::CellBox;

// Free cells kept around the sealed ones: whatever reaches them is open to space.
constexpr int kMargin = 1;

// The lowest cell by (z, y, x): rooms are ordered and named by it.
bool AnchorBefore(const Eigen::Vector3i& first, const Eigen::Vector3i& second) {
  return std::tuple(first.z(), first.y(), first.x()) < std::tuple(second.z(), second.y(), second.x());
}

Bounds Surround(const std::vector<CellBox>& boxes) {
  Eigen::Vector3i low = boxes.front().min;
  Eigen::Vector3i high = boxes.front().End();
  for (const CellBox& box : boxes) {
    low = low.cwiseMin(box.min);
    high = high.cwiseMax(box.End());
  }
  return {.low = low - Eigen::Vector3i::Constant(kMargin), .high = high + Eigen::Vector3i::Constant(kMargin)};
}

template <typename Visit>
void ForEachColumn(const CellBox& box, const Bounds& bounds, int width, Visit visit) {
  const int x_begin = box.min.x() - bounds.low.x();
  const int x_end = box.End().x() - bounds.low.x();
  for (int y = box.min.y() - bounds.low.y(); y < box.End().y() - bounds.low.y(); ++y) {
    const size_t row = static_cast<size_t>(y) * static_cast<size_t>(width);
    for (int x = x_begin; x < x_end; ++x) {
      visit(row + static_cast<size_t>(x));
    }
  }
}

SealedColumns SealedSpans(const std::vector<CellBox>& boxes, const Bounds& bounds, const Eigen::Vector2i& size) {
  SealedColumns sealed;
  sealed.offsets.assign(static_cast<size_t>(size.x()) * static_cast<size_t>(size.y()) + 1, 0);
  for (const CellBox& box : boxes) {
    ForEachColumn(box, bounds, size.x(), [&](size_t column) { ++sealed.offsets[column + 1]; });
  }
  std::partial_sum(sealed.offsets.begin(), sealed.offsets.end(), sealed.offsets.begin());
  sealed.spans.resize(sealed.offsets.back());
  std::vector<size_t> next(sealed.offsets.begin(), sealed.offsets.end() - 1);
  for (const CellBox& box : boxes) {
    const Span span {.begin = box.min.z(), .end = box.End().z()};
    ForEachColumn(box, bounds, size.x(), [&](size_t column) { sealed.spans[next[column]++] = span; });
  }
  for (size_t column = 0; column + 1 < sealed.offsets.size(); ++column) {
    const auto first = sealed.spans.begin() + static_cast<std::ptrdiff_t>(sealed.offsets[column]);
    const auto last = sealed.spans.begin() + static_cast<std::ptrdiff_t>(sealed.offsets[column + 1]);
    if (last - first > 1) {
      std::sort(first, last, [](const Span& a, const Span& b) { return a.begin < b.begin; });
    }
  }
  return sealed;
}

ColumnMap FreeColumns(const std::vector<CellBox>& boxes, const Bounds& bounds, const Eigen::Vector2i& size) {
  const SealedColumns sealed = SealedSpans(boxes, bounds, size);
  std::vector<size_t> offsets {0};
  std::vector<FreeInterval> intervals;
  for (size_t column = 0; column + 1 < sealed.offsets.size(); ++column) {
    int free_from = bounds.low.z();
    for (size_t i = sealed.offsets[column]; i < sealed.offsets[column + 1]; ++i) {
      const Span& span = sealed.spans[i];
      if (span.begin > free_from) {
        intervals.push_back({.begin = free_from, .end = span.begin});
      }
      free_from = std::max(free_from, span.end);
    }
    if (free_from < bounds.high.z()) {
      intervals.push_back({.begin = free_from, .end = bounds.high.z()});
    }
    offsets.push_back(intervals.size());
  }
  return ColumnMap(bounds.low.head<2>(), size, {bounds.low.z(), bounds.high.z()}, std::move(offsets),
                   std::move(intervals));
}

void JoinColumns(const ColumnMap& columns, size_t first, size_t second, DisjointSets& sets) {
  const std::span<const FreeInterval> all = columns.Intervals();
  size_t i = columns.FirstInterval(first);
  size_t j = columns.FirstInterval(second);
  const size_t i_end = columns.FirstInterval(first + 1);
  const size_t j_end = columns.FirstInterval(second + 1);
  while (i < i_end && j < j_end) {
    if (all[i].begin < all[j].end && all[j].begin < all[i].end) {
      sets.Union(i, j);
    }
    if (all[i].end < all[j].end) {
      ++i;
    } else {
      ++j;
    }
  }
}

void JoinNeighbours(const ColumnMap& columns, DisjointSets& sets) {
  const auto width = static_cast<size_t>(columns.Size().x());
  for (size_t column = 0; column < columns.ColumnCount(); ++column) {
    if (column % width + 1 < width) {
      JoinColumns(columns, column, column + 1, sets);
    }
    if (column + width < columns.ColumnCount()) {
      JoinColumns(columns, column, column + width, sets);
    }
  }
}

void Absorb(RoomExtent& room, int x, int y, const FreeInterval& interval) {
  if (std::tuple(interval.begin, y, x) < std::tuple(room.anchor_z, room.anchor_y, room.anchor_x)) {
    room.anchor_x = x;
    room.anchor_y = y;
    room.anchor_z = interval.begin;
  }
  room.low_x = std::min(room.low_x, x);
  room.low_y = std::min(room.low_y, y);
  room.high_x = std::max(room.high_x, x + 1);
  room.high_y = std::max(room.high_y, y + 1);
  room.low_z = std::min(room.low_z, interval.begin);
  room.high_z = std::max(room.high_z, interval.end);
  room.volume += interval.end - interval.begin;
}

RoomExtent ExtentOf(int x, int y, const FreeInterval& interval) {
  return {.anchor_x = x, .anchor_y = y, .anchor_z = interval.begin, .low_x = x, .low_y = y, .high_x = x + 1,
          .high_y = y + 1, .low_z = interval.begin, .high_z = interval.end,
          .volume = interval.end - interval.begin};
}

Room ToRoom(const RoomExtent& extent) {
  const Eigen::Vector3i low(extent.low_x, extent.low_y, extent.low_z);
  const Eigen::Vector3i high(extent.high_x, extent.high_y, extent.high_z);
  return {.anchor = {extent.anchor_x, extent.anchor_y, extent.anchor_z}, .volume_cells = extent.volume,
          .bounds = {.min = low, .extent = high - low}};
}

// Sets every interval's room: the vacuum for the ones open to space, otherwise rooms
// numbered by anchor. Returns the rooms, the vacuum first.
std::vector<Room> NameRooms(ColumnMap& columns, const Bounds& bounds, DisjointSets& sets) {
  const std::span<FreeInterval> intervals = columns.MutableIntervals();
  const size_t vacuum_node = intervals.size();
  const auto width = static_cast<size_t>(columns.Size().x());
  for (size_t column = 0; column < columns.ColumnCount(); ++column) {
    const bool edge = column % width == 0 || column % width + 1 == width || column < width ||
                      column + width >= columns.ColumnCount();
    for (size_t i = columns.FirstInterval(column); i < columns.FirstInterval(column + 1); ++i) {
      if (edge || intervals[i].begin == bounds.low.z() || intervals[i].end == bounds.high.z()) {
        sets.Union(i, vacuum_node);
      }
    }
  }
  const size_t vacuum_root = sets.Find(vacuum_node);
  constexpr size_t kNone = std::numeric_limits<size_t>::max();
  std::vector<size_t> part_of_root(intervals.size() + 1, kNone);
  std::vector<RoomExtent> parts;
  std::vector<size_t> part_of_interval(intervals.size(), kNone);
  for (size_t column = 0; column < columns.ColumnCount(); ++column) {
    const int x = columns.Origin().x() + static_cast<int>(column % width);
    const int y = columns.Origin().y() + static_cast<int>(column / width);
    for (size_t i = columns.FirstInterval(column); i < columns.FirstInterval(column + 1); ++i) {
      const size_t root = sets.Find(i);
      if (root == vacuum_root) {
        continue;
      }
      if (part_of_root[root] == kNone) {
        part_of_root[root] = parts.size();
        parts.push_back(ExtentOf(x, y, intervals[i]));
      } else {
        Absorb(parts[part_of_root[root]], x, y, intervals[i]);
      }
      part_of_interval[i] = part_of_root[root];
    }
  }
  std::vector<Room> named;
  named.reserve(parts.size());
  std::ranges::transform(parts, std::back_inserter(named), ToRoom);
  std::vector<size_t> by_anchor(parts.size());
  std::iota(by_anchor.begin(), by_anchor.end(), size_t {});
  std::ranges::sort(by_anchor, [&named](size_t a, size_t b) { return AnchorBefore(named[a].anchor, named[b].anchor); });
  std::vector<RoomIndex> index_of_part(parts.size());
  std::vector<Room> rooms {{.bounds = {.min = bounds.low, .extent = bounds.high - bounds.low}}};
  for (const size_t part : by_anchor) {
    index_of_part[part] = static_cast<RoomIndex>(rooms.size());
    rooms.push_back(named[part]);
  }
  for (size_t i = 0; i < intervals.size(); ++i) {
    intervals[i].room = part_of_interval[i] == kNone ? kVacuumRoom : index_of_part[part_of_interval[i]];
  }
  return rooms;
}

// The rooms on both sides of each cell of an opening's face, counted per pair of rooms.
void AddPortals(const RoomSources& sources, RoomGraph& graph) {
  std::vector<PortalSource> ordered = sources.portals;
  std::ranges::sort(ordered, [](const PortalSource& a, const PortalSource& b) {
    return AnchorBefore(a.opening.min, b.opening.min);
  });
  for (const PortalSource& source : ordered) {
    const CellBox& box = source.opening;
    const auto axis = static_cast<size_t>(source.axis);
    const int thickness = box.extent[static_cast<Eigen::Index>(axis)];
    std::array step {0, 0, 0};
    step[axis] = 1;
    CellBox face = box;
    face.extent[static_cast<Eigen::Index>(axis)] = 1;
    // Few pairs per opening: a linear search beats a map for every cell.
    std::vector<std::pair<std::pair<RoomIndex, RoomIndex>, int>> areas;
    for (int z = face.min.z(); z < face.End().z(); ++z) {
      for (int y = face.min.y(); y < face.End().y(); ++y) {
        for (int x = face.min.x(); x < face.End().x(); ++x) {
          const auto before = graph.columns.RoomAt(x - step[0], y - step[1], z - step[2]);
          const auto after =
              graph.columns.RoomAt(x + step[0] * thickness, y + step[1] * thickness, z + step[2] * thickness);
          if (before && after && *before != *after) {
            const std::pair<RoomIndex, RoomIndex> pair {std::min(*before, *after), std::max(*before, *after)};
            const auto found = std::ranges::find(areas, pair, &decltype(areas)::value_type::first);
            if (found == areas.end()) {
              areas.emplace_back(pair, 1);
            } else {
              ++found->second;
            }
          }
        }
      }
    }
    std::ranges::sort(areas);
    for (const auto& [pair, area] : areas) {
      graph.portals.push_back({.a = pair.first,
                               .b = pair.second,
                               .area_cells = area,
                               .visible = source.visible,
                               .passable = source.passable,
                               .opening = source.opening});
    }
  }
}

}  // namespace

std::expected<RoomGraph, std::string> BuildRooms(const RoomSources& sources) {
  RoomGraph graph;
  if (sources.sealed.empty()) {
    graph.rooms.push_back({});
    return graph;
  }
  const Bounds bounds = Surround(sources.sealed);
  const Eigen::Vector2i size = (bounds.high - bounds.low).head<2>();
  if (static_cast<size_t>(size.x()) * static_cast<size_t>(size.y()) > kMaxColumns) {
    return std::unexpected(std::format(
        "the station spans {}x{} columns, more than the {} rooms are built over", size.x(), size.y(), kMaxColumns));
  }
  graph.columns = FreeColumns(sources.sealed, bounds, size);
  DisjointSets sets(graph.columns.Intervals().size() + 1);
  JoinNeighbours(graph.columns, sets);
  graph.rooms = NameRooms(graph.columns, bounds, sets);
  AddPortals(sources, graph);
  return graph;
}

}  // namespace z13::station::rooms
