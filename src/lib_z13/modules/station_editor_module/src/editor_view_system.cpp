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

#include "editor_view_system.h"

#include <format>
#include <string>
#include <utility>
#include <variant>

#include <flecs.h>

#include <lib_core/state/world_state.h>
#include <lib_core/world/lifecycle.h>
#include <station_graph/graph_diagnosis.h>
#include <station_graph/graph_editor.h>
#include <z13/components/editor/editor_view.h>

#include "editor_graph.h"

namespace z13::station_editor {

namespace {

namespace graph = z13::editor::graph;
using z13::editor::EditorItemView;
using z13::editor::EditorPropertyKind;
using z13::editor::EditorPropertyView;
using z13::editor::EditorView;

EditorPropertyKind KindOf(graph::PropertyKind kind) {
  switch (kind) {
    case graph::PropertyKind::kInt:
      return EditorPropertyKind::kInt;
    case graph::PropertyKind::kBool:
      return EditorPropertyKind::kBool;
    case graph::PropertyKind::kEnum:
      return EditorPropertyKind::kEnum;
  }
  return EditorPropertyKind::kInt;
}

std::string TextOf(const graph::PropertyValue& value) {
  struct Text {
    std::string operator()(bool flag) const { return flag ? "true" : "false"; }
    std::string operator()(int number) const { return std::to_string(number); }
    std::string operator()(const std::string& name) const { return name; }
  };
  return std::visit(Text {}, value);
}

std::vector<EditorPropertyView> PropertiesOf(const graph::PropertySpecs& specs, const graph::PropertyMap& given) {
  const graph::PropertyMap values = graph::WithDefaults(specs, given);
  std::vector<EditorPropertyView> properties;
  for (const auto& [name, spec] : specs) {
    properties.push_back({.name = name,
                          .title = spec.title,
                          .unit = spec.unit,
                          .kind = KindOf(spec.kind),
                          .min = spec.min,
                          .max = spec.max,
                          .values = spec.values,
                          .value = TextOf(values.at(name))});
  }
  return properties;
}

EditorView ViewOf(const graph::GraphEditor& editor, bool open) {
  const graph::Graph& graph = editor.graph();
  const graph::PropertySchema& schema = editor.schema();
  EditorView view {.open = open, .loaded = true, .problems = graph::Diagnose(graph, schema)};
  for (const graph::Node& node : graph.nodes) {
    view.rooms.push_back({.id = node.id,
                          .label = node.name.empty() ? node.id : node.name,
                          .detail = std::format("at {}, {}, {}; {} x {} cells", node.box.origin.x(), node.box.origin.y(),
                                                node.box.origin.z(), node.box.size.x(), node.box.size.y()),
                          .box = z13::editor::EditorBox {.origin = node.box.origin, .size = node.box.size},
                          .properties = PropertiesOf(schema.node, node.properties)});
  }
  for (const graph::Edge& edge : graph.edges) {
    view.edges.push_back({.id = edge.id,
                          .label = std::format("{}: {} - {}", edge.id, edge.from, edge.to),
                          .properties = PropertiesOf(schema.edge, edge.properties)});
  }
  return view;
}

void RegisterSystems(flecs::world world) {
  world.system<EditorView, const EditorGraph>("EditorViewSystem::Update")
      .kind(flecs::PreFrame)
      .each([](EditorView& view, const EditorGraph& graph) {
        if (!view.open) {
          return;
        }
        auto selected = std::move(view.selected);
        view = graph.editor ? ViewOf(*graph.editor, view.open) : EditorView {.open = view.open};
        view.selected = std::move(selected);
      });
}

}  // namespace

void EditorViewSystem::Register(flecs::world& world) {
  z13::OnRegisterComponents(world, [](flecs::world& w) { flecs_tools::RegisterComponent<EditorView>(w); });
  z13::OnInitSystems(world, [](flecs::world& w) {
    w.set(EditorView {});
    RegisterSystems(w);
  });
}

}  // namespace z13::station_editor
