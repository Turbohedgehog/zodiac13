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

#include "station_graph/graph_editor.h"

#include <algorithm>
#include <format>
#include <utility>

namespace z13::editor::graph {

namespace {

z13::Status CheckBox(const RoomBox& box) {
  if (box.size.x() < kMinRoomCells || box.size.y() < kMinRoomCells) {
    return std::unexpected(std::format("a room is at least {} cells across, not {}x{}", kMinRoomCells, box.size.x(),
                                       box.size.y()));
  }
  return {};
}

template <typename Item>
z13::Status SetProperty(Item& item, const PropertySpecs& specs, std::string_view name, PropertyValue value) {
  PropertyMap changed = item.properties;
  changed.insert_or_assign(std::string(name), std::move(value));
  if (const z13::Status valid = CheckProperties(specs, changed); !valid) {
    return valid;
  }
  item.properties = std::move(changed);
  return {};
}

}  // namespace

GraphEditor::GraphEditor(Graph graph, PropertySchema schema) : graph_(std::move(graph)), schema_(std::move(schema)) {}

z13::Status GraphEditor::Edit(const std::function<z13::Status(Graph&)>& change) {
  Graph changed = graph_;
  if (const z13::Status done = change(changed); !done) {
    return done;
  }
  if (undo_.size() == kMaxHistory) {
    undo_.erase(undo_.begin());
  }
  undo_.push_back(std::exchange(graph_, std::move(changed)));
  redo_.clear();
  return {};
}

z13::Status GraphEditor::AddRoom(Node room) {
  return Edit([this, &room](Graph& graph) -> z13::Status {
    if (room.id.empty()) {
      return std::unexpected("a room needs an id");
    }
    if (FindNode(graph, room.id)) {
      return std::unexpected(std::format("a room '{}' exists already", room.id));
    }
    if (const z13::Status box = CheckBox(room.box); !box) {
      return box;
    }
    if (const z13::Status valid = CheckProperties(schema_.node, room.properties); !valid) {
      return std::unexpected(std::format("room '{}': {}", room.id, valid.error()));
    }
    if (room.name.empty()) {
      room.name = room.id;
    }
    graph.nodes.push_back(std::move(room));
    return {};
  });
}

z13::Status GraphEditor::RemoveNode(std::string_view id) {
  return Edit([id](Graph& graph) -> z13::Status {
    const auto index = FindNode(graph, id);
    if (!index) {
      return std::unexpected(std::format("no room '{}'", id));
    }
    graph.nodes.erase(graph.nodes.begin() + static_cast<std::ptrdiff_t>(*index));
    std::erase_if(graph.edges, [id](const Edge& edge) { return edge.from == id || edge.to == id; });
    return {};
  });
}

z13::Status GraphEditor::SetRoomBox(std::string_view id, const RoomBox& box) {
  return Edit([id, &box](Graph& graph) -> z13::Status {
    const auto index = FindNode(graph, id);
    if (!index) {
      return std::unexpected(std::format("no room '{}'", id));
    }
    if (const z13::Status valid = CheckBox(box); !valid) {
      return valid;
    }
    graph.nodes[*index].box = box;
    return {};
  });
}

z13::Status GraphEditor::SetNodeProperty(std::string_view id, std::string_view name, PropertyValue value) {
  return Edit([this, id, name, &value](Graph& graph) -> z13::Status {
    const auto index = FindNode(graph, id);
    if (!index) {
      return std::unexpected(std::format("no room '{}'", id));
    }
    return SetProperty(graph.nodes[*index], schema_.node, name, std::move(value));
  });
}

z13::Status GraphEditor::AddEdge(Edge edge) {
  return Edit([this, &edge](Graph& graph) -> z13::Status {
    if (edge.id.empty()) {
      return std::unexpected("an edge needs an id");
    }
    if (FindEdge(graph, edge.id)) {
      return std::unexpected(std::format("an edge '{}' exists already", edge.id));
    }
    if (edge.from == edge.to) {
      return std::unexpected(std::format("edge '{}' joins a room to itself", edge.id));
    }
    for (const std::string& end : {edge.from, edge.to}) {
      if (!FindNode(graph, end)) {
        return std::unexpected(std::format("edge '{}': no room '{}'", edge.id, end));
      }
    }
    if (const z13::Status valid = CheckProperties(schema_.edge, edge.properties); !valid) {
      return std::unexpected(std::format("edge '{}': {}", edge.id, valid.error()));
    }
    graph.edges.push_back(std::move(edge));
    return {};
  });
}

z13::Status GraphEditor::RemoveEdge(std::string_view id) {
  return Edit([id](Graph& graph) -> z13::Status {
    const auto index = FindEdge(graph, id);
    if (!index) {
      return std::unexpected(std::format("no edge '{}'", id));
    }
    graph.edges.erase(graph.edges.begin() + static_cast<std::ptrdiff_t>(*index));
    return {};
  });
}

z13::Status GraphEditor::SetEdgeProperty(std::string_view id, std::string_view name, PropertyValue value) {
  return Edit([this, id, name, &value](Graph& graph) -> z13::Status {
    const auto index = FindEdge(graph, id);
    if (!index) {
      return std::unexpected(std::format("no edge '{}'", id));
    }
    return SetProperty(graph.edges[*index], schema_.edge, name, std::move(value));
  });
}

z13::Status GraphEditor::Undo() {
  if (undo_.empty()) {
    return std::unexpected("nothing to undo");
  }
  redo_.push_back(std::exchange(graph_, std::move(undo_.back())));
  undo_.pop_back();
  return {};
}

z13::Status GraphEditor::Redo() {
  if (redo_.empty()) {
    return std::unexpected("nothing to redo");
  }
  undo_.push_back(std::exchange(graph_, std::move(redo_.back())));
  redo_.pop_back();
  return {};
}

void GraphEditor::Replace(Graph graph) {
  graph_ = std::move(graph);
  undo_.clear();
  redo_.clear();
}

}  // namespace z13::editor::graph
