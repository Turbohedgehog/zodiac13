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

#include "station_graph/graph_json.h"

#include <algorithm>
#include <format>
#include <optional>
#include <vector>

#include <rfl.hpp>
#include <rfl/json.hpp>

namespace z13::editor::graph {

namespace {

// The file's shape; the graph's types hold Eigen vectors, which the file spells out as numbers.
struct BoxJson {
  int x {};
  int y {};
  int z {};
  int w {};
  int d {};
};

struct NodeJson {
  std::string id;
  std::optional<std::string> name;
  BoxJson box;
  std::optional<PropertyMap> properties;
};

struct EdgeJson {
  std::string id;
  std::string from;
  std::string to;
  std::optional<PropertyMap> properties;
};

struct GraphJson {
  int version {};
  std::vector<NodeJson> nodes;
  std::vector<EdgeJson> edges;
};

Node NodeOf(const NodeJson& json) {
  return {.id = json.id,
          .name = json.name.value_or(json.id),
          .box = {.origin = {json.box.x, json.box.y, json.box.z}, .size = {json.box.w, json.box.d}},
          .properties = json.properties.value_or(PropertyMap {})};
}

NodeJson JsonOf(const Node& node) {
  return {.id = node.id,
          .name = node.name,
          .box = {.x = node.box.origin.x(),
                  .y = node.box.origin.y(),
                  .z = node.box.origin.z(),
                  .w = node.box.size.x(),
                  .d = node.box.size.y()},
          .properties = node.properties};
}

}  // namespace

std::expected<Graph, std::string> ParseGraph(std::string_view json) {
  const auto parsed = rfl::json::read<GraphJson>(std::string(json));
  if (!parsed) {
    return std::unexpected(std::format("graph: {}", parsed.error().what()));
  }
  if (parsed->version != kGraphVersion) {
    return std::unexpected(std::format("graph version {}, expected {}", parsed->version, kGraphVersion));
  }
  Graph graph;
  std::ranges::transform(parsed->nodes, std::back_inserter(graph.nodes), NodeOf);
  std::ranges::transform(parsed->edges, std::back_inserter(graph.edges), [](const EdgeJson& edge) {
    return Edge {.id = edge.id,
                 .from = edge.from,
                 .to = edge.to,
                 .properties = edge.properties.value_or(PropertyMap {})};
  });
  return graph;
}

std::string WriteGraph(const Graph& graph) {
  GraphJson json {.version = kGraphVersion};
  std::ranges::transform(graph.nodes, std::back_inserter(json.nodes), JsonOf);
  std::ranges::transform(graph.edges, std::back_inserter(json.edges), [](const Edge& edge) {
    return EdgeJson {.id = edge.id, .from = edge.from, .to = edge.to, .properties = edge.properties};
  });
  return rfl::json::write(json, rfl::json::pretty);
}

}  // namespace z13::editor::graph
