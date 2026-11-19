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

#include "editor_overlay_system.h"

#include <algorithm>
#include <array>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include <flecs.h>

#include <lib_core/world/lifecycle.h>
#include <station_graph/graph_diagnosis.h>
#include <station_graph/graph_editor.h>
#include <station_graph/graph_geometry.h>
#include <z13/components/editor/editor_view.h>
#include <z13/components/station/overlay_layers.h>

#include "editor_graph.h"

namespace z13::station_editor {

namespace {

namespace graph = z13::editor::graph;
using z13::editor::EditorTarget;
using z13::station::OverlayBox;
using z13::station::OverlayLayers;

constexpr std::string_view kLayerName = "graph";
constexpr std::string_view kKindProperty = "kind";

constexpr z13::Rgba kRoomColor {120, 170, 255, 255};
constexpr z13::Rgba kDoorColor {90, 220, 110, 255};
constexpr z13::Rgba kWindowColor {110, 220, 230, 255};
constexpr z13::Rgba kPassageColor {240, 220, 70, 255};
constexpr z13::Rgba kUnknownKindColor {220, 70, 60, 255};
constexpr z13::Rgba kSelectedColor {255, 255, 255, 255};

struct KindColor {
  std::string_view kind;
  z13::Rgba color;
};

// The schema's enumerators of an edge's kind (assets/station/graph/properties.json).
constexpr std::array kKindColors {KindColor {"Door", kDoorColor}, KindColor {"Window", kWindowColor},
                                  KindColor {"Passage", kPassageColor}};

OverlayBox BoxOf(const graph::CellBox& cells, const z13::Rgba& color) {
  return {.min = cells.min, .extent = cells.extent, .color = color};
}

z13::Rgba ColorOfKind(const graph::Edge& edge, const graph::PropertySchema& schema) {
  const graph::PropertyMap values = graph::WithDefaults(schema.edge, edge.properties);
  const auto kind = values.find(std::string(kKindProperty));
  if (kind == values.end() || !std::holds_alternative<std::string>(kind->second)) {
    return kUnknownKindColor;
  }
  const auto color = std::ranges::find(kKindColors, std::get<std::string>(kind->second), &KindColor::kind);
  return color != kKindColors.end() ? color->color : kUnknownKindColor;
}

void Fill(z13::station::OverlayLayer& layer, const graph::GraphEditor& editor,
          const std::optional<z13::editor::EditorSelection>& selected) {
  const graph::Graph& graph = editor.graph();
  const graph::PropertySchema& schema = editor.schema();
  layer.boxes.clear();
  for (const graph::Node& node : graph.nodes) {
    const bool picked = selected && selected->target == EditorTarget::kRoom && selected->id == node.id;
    layer.boxes.push_back(BoxOf(graph::CellsOfRoom(node, schema), picked ? kSelectedColor : kRoomColor));
  }
  for (const graph::Edge& edge : graph.edges) {
    const auto from = graph::FindNode(graph, edge.from);
    const auto to = graph::FindNode(graph, edge.to);
    const auto wall = from && to ? graph::SharedWall(graph.nodes[*from], graph.nodes[*to], schema) : std::nullopt;
    if (wall) {
      const bool picked = selected && selected->target == EditorTarget::kEdge && selected->id == edge.id;
      layer.boxes.push_back(BoxOf(*wall, picked ? kSelectedColor : ColorOfKind(edge, schema)));
    }
  }
  layer.label = std::format("Station graph: {} rooms, {} edges, {} problems", graph.nodes.size(), graph.edges.size(),
                            graph::Diagnose(graph, schema).size());
}

void RegisterSystems(flecs::world world) {
  // station_module makes the layers; without it (a headless world of the editor alone) there is nothing to fill.
  if (auto* layers = world.try_get_mut<OverlayLayers>()) {
    layers->layers.push_back({.name = std::string(kLayerName)});
  }
  world.system<OverlayLayers, const EditorGraph, const z13::editor::EditorView>("EditorOverlaySystem::Update")
      .kind(flecs::PreFrame)  // a frame phase: the editor works in the menu and on pause as well
      .each([](flecs::iter&, size_t, OverlayLayers& layers, const EditorGraph& graph, const z13::editor::EditorView& view) {
        z13::station::OverlayLayer* layer = layers.Find(std::string(kLayerName));
        if (layer == nullptr) {
          return;
        }
        if (!layers.IsShown(layer->name) || !graph.editor) {
          *layer = {.name = layer->name};
          return;
        }
        Fill(*layer, *graph.editor, view.selected);
      });
}

}  // namespace

void EditorOverlaySystem::Register(flecs::world& world) {
  z13::OnInitSystems(world, RegisterSystems);
}

}  // namespace z13::station_editor
