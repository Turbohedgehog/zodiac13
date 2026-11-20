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

#include "station_graph/graph_decks.h"

#include <algorithm>
#include <cstddef>
#include <iterator>

#include "station_graph/graph_geometry.h"

namespace z13::editor::graph {

namespace {

// The ceiling slab is a cell thick; the cut goes half a cell below it so the slab's underside is never at the cut.
constexpr float kCutBelowTopCells = 1.5f;

}  // namespace

std::vector<float> DeckCuts(const Graph& graph, const PropertySchema& schema) {
  std::vector<float> cuts;
  std::ranges::transform(graph.nodes, std::back_inserter(cuts), [&schema](const Node& node) {
    const CellBox cells = CellsOfRoom(node, schema);
    return static_cast<float>(cells.min.z() + cells.extent.z()) - kCutBelowTopCells;
  });
  std::ranges::sort(cuts);
  cuts.erase(std::ranges::unique(cuts).begin(), cuts.end());
  return cuts;
}

std::optional<float> SteppedCut(const std::vector<float>& cuts, std::optional<float> current, int step) {
  const auto position = static_cast<std::ptrdiff_t>(
      current ? std::distance(cuts.begin(), std::ranges::lower_bound(cuts, *current)) : std::ssize(cuts));
  const std::ptrdiff_t target = std::max<std::ptrdiff_t>(position + step, 0);
  if (target >= std::ssize(cuts)) {
    return std::nullopt;
  }
  return cuts[static_cast<std::size_t>(target)];
}

}  // namespace z13::editor::graph
