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

#include "station_graph/graph_geometry.h"

#include <algorithm>
#include <string>

namespace z13::editor::graph {

namespace {

// The floor slab and the ceiling slab around a room's free height.
constexpr int kSlabsCells = 2;

// Between the wall rings of the room.
Span Inside(const RoomBox& box, int axis) {
  const Span whole = Along(box, axis);
  return {.low = whole.low + 1, .high = whole.high - 1};
}

// The wall across `axis` the boxes share, if they meet by one cell along it.
std::optional<CellBox> WallAcross(const RoomBox& a, const RoomBox& b, int axis, int height) {
  const int other = 1 - axis;
  const auto touching = Overlap(Along(a, axis), Along(b, axis));
  const auto stretch = Overlap(Inside(a, other), Inside(b, other));
  if (!touching || touching->Length() != 1 || !stretch) {
    return std::nullopt;
  }
  CellBox wall {.extent = Eigen::Vector3i::Ones()};
  wall.min[axis] = touching->low;
  wall.min[other] = stretch->low;
  wall.extent[other] = stretch->Length();
  wall.min.z() = a.origin.z();
  wall.extent.z() = height + kSlabsCells;
  return wall;
}

}  // namespace

std::optional<Span> Overlap(const Span& a, const Span& b) {
  const Span common {.low = std::max(a.low, b.low), .high = std::min(a.high, b.high)};
  if (common.Length() <= 0) {
    return std::nullopt;
  }
  return common;
}

Span Along(const RoomBox& box, int axis) {
  return {.low = box.origin[axis], .high = box.origin[axis] + box.size[axis]};
}

int HeightOf(const Node& node, const PropertySchema& schema) {
  const PropertyMap values = WithDefaults(schema.node, node.properties);
  const auto height = values.find(std::string(kHeightProperty));
  return height != values.end() ? std::get<int>(height->second) : 0;
}

CellBox CellsOfRoom(const Node& node, const PropertySchema& schema) {
  return {.min = node.box.origin,
          .extent = {node.box.size.x(), node.box.size.y(), HeightOf(node, schema) + kSlabsCells}};
}

std::optional<CellBox> SharedWall(const Node& a, const Node& b, const PropertySchema& schema) {
  if (a.box.origin.z() != b.box.origin.z()) {
    return std::nullopt;
  }
  const int height = std::max(HeightOf(a, schema), HeightOf(b, schema));
  for (const int axis : {0, 1}) {
    if (const auto wall = WallAcross(a.box, b.box, axis, height)) {
      return wall;
    }
  }
  return std::nullopt;
}

}  // namespace z13::editor::graph
