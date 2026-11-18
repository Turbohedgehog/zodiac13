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
#include <functional>
#include <string_view>
#include <vector>

#include <lib_core/utils/status.h>

#include "station_graph/graph.h"
#include "station_graph/property_schema.h"

namespace z13::editor::graph {

// A room is at least this many cells across: two walls and a free cell between them to stand in.
inline constexpr int kMinRoomCells = 5;
inline constexpr std::size_t kMaxHistory = 100;

// The graph and its edits. An edit that would leave the graph out of shape (a duplicate id, an edge to nowhere, a
// property outside its range) is refused with the reason and changes nothing; one that only makes the station
// unbuildable (rooms overlapping) is allowed, and Diagnose says what is wrong. Every edit can be undone.
class GraphEditor {
 public:
  GraphEditor(Graph graph, PropertySchema schema);

  const Graph& graph() const { return graph_; }
  const PropertySchema& schema() const { return schema_; }

  z13::Status AddRoom(Node room);
  z13::Status RemoveNode(std::string_view id);
  z13::Status SetRoomBox(std::string_view id, const RoomBox& box);
  z13::Status SetNodeProperty(std::string_view id, std::string_view name, PropertyValue value);
  z13::Status AddEdge(Edge edge);
  z13::Status RemoveEdge(std::string_view id);
  z13::Status SetEdgeProperty(std::string_view id, std::string_view name, PropertyValue value);

  bool CanUndo() const { return !undo_.empty(); }
  bool CanRedo() const { return !redo_.empty(); }
  z13::Status Undo();
  z13::Status Redo();

  void Replace(Graph graph);

 private:
  // Applies `change` to a copy and keeps it, with the old graph on the undo list, only when it succeeds.
  z13::Status Edit(const std::function<z13::Status(Graph&)>& change);

  Graph graph_;
  PropertySchema schema_;
  std::vector<Graph> undo_;
  std::vector<Graph> redo_;
};

}  // namespace z13::editor::graph
