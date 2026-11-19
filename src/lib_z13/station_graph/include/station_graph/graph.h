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

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <Eigen/Dense>

// The station graph of the editor (docs/station-editor-plan.md): rooms and the openings between them, with
// properties that decide how the station is built. The graph is the source; blueprints are its output.
namespace z13::editor::graph {

inline constexpr int kGraphVersion = 1;

using PropertyValue = std::variant<bool, int, std::string>;
using PropertyMap = std::map<std::string, PropertyValue>;

// In cells, the wall ring included: two rooms that meet share one wall cell line.
struct RoomBox {
  Eigen::Vector3i origin = Eigen::Vector3i::Zero();
  Eigen::Vector2i size = Eigen::Vector2i::Zero();

  bool operator==(const RoomBox&) const = default;
};

struct Node {
  std::string id;
  std::string name;
  RoomBox box;
  // Only what differs from the schema's defaults is stored.
  PropertyMap properties;

  bool operator==(const Node&) const = default;
};

struct Edge {
  std::string id;
  std::string from;
  std::string to;
  PropertyMap properties;

  bool operator==(const Edge&) const = default;
};

struct Graph {
  std::vector<Node> nodes;
  std::vector<Edge> edges;

  bool operator==(const Graph&) const = default;
};

std::optional<std::size_t> FindNode(const Graph& graph, std::string_view id);
std::optional<std::size_t> FindEdge(const Graph& graph, std::string_view id);

}  // namespace z13::editor::graph
