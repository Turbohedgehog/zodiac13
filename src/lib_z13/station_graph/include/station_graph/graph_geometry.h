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

#include <optional>
#include <string_view>

#include <Eigen/Dense>

#include "station_graph/graph.h"
#include "station_graph/property_schema.h"

namespace z13::editor::graph {

inline constexpr std::string_view kHeightProperty = "height";

// A half-open stretch of cells along one axis (0 is x, 1 is y).
struct Span {
  int low {};
  int high {};

  int Length() const { return high - low; }
};

std::optional<Span> Overlap(const Span& a, const Span& b);

// The room's box along `axis`, the wall rings included.
Span Along(const RoomBox& box, int axis);

struct CellBox {
  Eigen::Vector3i min = Eigen::Vector3i::Zero();
  Eigen::Vector3i extent = Eigen::Vector3i::Zero();

  bool operator==(const CellBox&) const = default;
};

// The room's free height, the schema's default where the node leaves it out.
int HeightOf(const Node& node, const PropertySchema& schema);

// Everything the room takes: the floor slab, the free height and the ceiling slab, the wall ring included.
CellBox CellsOfRoom(const Node& node, const PropertySchema& schema);

// The wall cells two rooms on one level share (boxes overlapping by one cell along one axis, inner faces by more
// along the other), as tall as the taller room.
std::optional<CellBox> SharedWall(const Node& a, const Node& b, const PropertySchema& schema);

}  // namespace z13::editor::graph
