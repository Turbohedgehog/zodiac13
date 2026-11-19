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

#include "station_graph/graph_diagnosis.h"

#include <algorithm>
#include <format>
#include <optional>

namespace z13::editor::graph {

namespace {

// A half-open stretch of cells along one axis.
struct Span {
  int low {};
  int high {};

  int Length() const { return high - low; }
};

std::optional<Span> Overlap(const Span& a, const Span& b) {
  const Span common {.low = std::max(a.low, b.low), .high = std::min(a.high, b.high)};
  if (common.Length() <= 0) {
    return std::nullopt;
  }
  return common;
}

Span XSpan(const RoomBox& box) {
  return {.low = box.origin.x(), .high = box.origin.x() + box.size.x()};
}

Span YSpan(const RoomBox& box) {
  return {.low = box.origin.y(), .high = box.origin.y() + box.size.y()};
}

// Rooms on one level share a wall when their boxes overlap by one cell along one axis and by more along the other.
bool ShareWall(const RoomBox& a, const RoomBox& b) {
  if (a.origin.z() != b.origin.z()) {
    return false;
  }
  const auto across_x = Overlap(XSpan(a), XSpan(b));
  const auto across_y = Overlap(YSpan(a), YSpan(b));
  if (!across_x || !across_y) {
    return false;
  }
  return across_x->Length() == 1 || across_y->Length() == 1;
}

bool OverlapMoreThanAWall(const RoomBox& a, const RoomBox& b) {
  if (a.origin.z() != b.origin.z()) {
    return false;
  }
  const auto across_x = Overlap(XSpan(a), XSpan(b));
  const auto across_y = Overlap(YSpan(a), YSpan(b));
  return across_x && across_y && across_x->Length() > 1 && across_y->Length() > 1;
}

}  // namespace

std::vector<std::string> Diagnose(const Graph& graph) {
  std::vector<std::string> problems;
  for (std::size_t i = 0; i < graph.nodes.size(); ++i) {
    for (std::size_t j = i + 1; j < graph.nodes.size(); ++j) {
      if (OverlapMoreThanAWall(graph.nodes[i].box, graph.nodes[j].box)) {
        problems.push_back(
            std::format("rooms '{}' and '{}' overlap by more than a shared wall", graph.nodes[i].id, graph.nodes[j].id));
      }
    }
  }
  for (const Edge& edge : graph.edges) {
    const auto from = FindNode(graph, edge.from);
    const auto to = FindNode(graph, edge.to);
    if (from && to && !ShareWall(graph.nodes[*from].box, graph.nodes[*to].box)) {
      problems.push_back(std::format("edge '{}': rooms '{}' and '{}' share no wall", edge.id, edge.from, edge.to));
    }
  }
  return problems;
}

}  // namespace z13::editor::graph
