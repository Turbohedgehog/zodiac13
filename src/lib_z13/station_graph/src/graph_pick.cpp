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

#include "station_graph/graph_pick.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#include "station_graph/graph_geometry.h"

namespace z13::editor::graph {

namespace {

// A direction component under this is parallel to the slab it is tested against.
constexpr float kParallel = 1e-6f;
// How far behind the face of a room it enters a ray still means that face's wall.
constexpr float kWallCells = 1.f;

struct Crossing {
  float entry {};
  float exit {};
};

// Where the ray is inside the box, from the point it enters (0 when it starts inside) to the point it leaves.
std::optional<Crossing> Cross(const CellBox& box, const Eigen::Vector3f& origin, const Eigen::Vector3f& direction) {
  float near = 0.f;
  float far = std::numeric_limits<float>::max();
  for (int axis = 0; axis < 3; ++axis) {
    const float low = static_cast<float>(box.min[axis]);
    const float high = static_cast<float>(box.min[axis] + box.extent[axis]);
    if (std::abs(direction[axis]) < kParallel) {
      if (origin[axis] < low || origin[axis] > high) {
        return std::nullopt;
      }
      continue;
    }
    float first = (low - origin[axis]) / direction[axis];
    float second = (high - origin[axis]) / direction[axis];
    if (first > second) {
      std::swap(first, second);
    }
    near = std::max(near, first);
    far = std::min(far, second);
  }
  if (near > far) {
    return std::nullopt;
  }
  return Crossing {.entry = near, .exit = far};
}

struct Hit {
  const Node* room {};
  Crossing crossing;
};

std::optional<Hit> NearestRoom(const Graph& graph, const PropertySchema& schema, const Eigen::Vector3f& origin,
                               const Eigen::Vector3f& direction) {
  std::optional<Hit> nearest;
  for (const Node& node : graph.nodes) {
    const auto crossing = Cross(CellsOfRoom(node, schema), origin, direction);
    if (crossing && (!nearest || crossing->entry < nearest->crossing.entry)) {
      nearest = Hit {.room = &node, .crossing = *crossing};
    }
  }
  return nearest;
}

}  // namespace

std::optional<Pick> PickAlong(const Graph& graph, const PropertySchema& schema, const Eigen::Vector3f& origin,
                              const Eigen::Vector3f& direction) {
  const auto room = NearestRoom(graph, schema, origin, direction);
  if (!room) {
    return std::nullopt;
  }
  std::optional<Pick> pick = Pick {.kind = PickKind::kRoom, .id = room->room->id, .distance = room->crossing.entry};
  // From inside any wall of the room is aimed at; from outside only the one entered through, not one seen through it.
  float nearest_edge = room->crossing.entry > 0.f ? room->crossing.entry + kWallCells : room->crossing.exit;
  for (const Edge& edge : graph.edges) {
    const auto from = FindNode(graph, edge.from);
    const auto to = FindNode(graph, edge.to);
    const auto wall = from && to ? SharedWall(graph.nodes[*from], graph.nodes[*to], schema) : std::nullopt;
    const auto crossing = wall ? Cross(*wall, origin, direction) : std::nullopt;
    if (crossing && crossing->entry <= nearest_edge) {
      nearest_edge = crossing->entry;
      pick = Pick {.kind = PickKind::kEdge, .id = edge.id, .distance = crossing->entry};
    }
  }
  return pick;
}

}  // namespace z13::editor::graph
