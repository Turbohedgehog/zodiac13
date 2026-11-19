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

#include <cstdint>
#include <optional>
#include <string>

#include <Eigen/Dense>

#include "station_graph/graph.h"
#include "station_graph/property_schema.h"

namespace z13::editor::graph {

enum class PickKind : std::uint8_t { kRoom, kEdge };

struct Pick {
  PickKind kind {};
  std::string id;
  // Along the ray, in cells.
  float distance {};
};

// What a ray in cells points at: the opening in the wall it meets first (from inside a room, any wall of it), else the
// room; the walls are inside the rooms' boxes, so the nearest box alone would never choose an opening.
std::optional<Pick> PickAlong(const Graph& graph, const PropertySchema& schema, const Eigen::Vector3f& origin,
                              const Eigen::Vector3f& direction);

}  // namespace z13::editor::graph
