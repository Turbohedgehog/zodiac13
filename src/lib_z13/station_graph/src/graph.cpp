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

#include "station_graph/graph.h"

#include <algorithm>

namespace z13::editor::graph {

namespace {

template <typename Items>
std::optional<std::size_t> IndexOfId(const Items& items, std::string_view id) {
  const auto found = std::ranges::find(items, id, &Items::value_type::id);
  if (found == items.end()) {
    return std::nullopt;
  }
  return static_cast<std::size_t>(found - items.begin());
}

}  // namespace

std::optional<std::size_t> FindNode(const Graph& graph, std::string_view id) {
  return IndexOfId(graph.nodes, id);
}

std::optional<std::size_t> FindEdge(const Graph& graph, std::string_view id) {
  return IndexOfId(graph.edges, id);
}

}  // namespace z13::editor::graph
