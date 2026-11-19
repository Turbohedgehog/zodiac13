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

#include <format>
#include <optional>

#include "station_graph/graph_geometry.h"

namespace z13::editor::graph {

namespace {

bool OverlapMoreThanAWall(const RoomBox& a, const RoomBox& b) {
  if (a.origin.z() != b.origin.z()) {
    return false;
  }
  const auto across_x = Overlap(Along(a, 0), Along(b, 0));
  const auto across_y = Overlap(Along(a, 1), Along(b, 1));
  return across_x && across_y && across_x->Length() > 1 && across_y->Length() > 1;
}

}  // namespace

std::vector<std::string> Diagnose(const Graph& graph, const PropertySchema& schema) {
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
    if (from && to && !SharedWall(graph.nodes[*from], graph.nodes[*to], schema)) {
      problems.push_back(std::format("edge '{}': rooms '{}' and '{}' share no wall", edge.id, edge.from, edge.to));
    }
  }
  return problems;
}

}  // namespace z13::editor::graph
