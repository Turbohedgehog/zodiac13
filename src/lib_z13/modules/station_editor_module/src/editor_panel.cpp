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

#include "editor_panel.h"

#include <algorithm>
#include <array>
#include <format>
#include <string>
#include <string_view>

#include <z13/components/editor/editor_view.h>

namespace z13::station_editor {

namespace {

using z13::editor::EditorItemView;
using z13::editor::EditorPropertyKind;
using z13::editor::EditorPropertyView;
using z13::editor::EditorTarget;
using z13::gui::PanelAction;
using z13::gui::PanelWidget;
using z13::gui::PanelWidgetKind;

constexpr std::string_view kNodeTarget = "node";
constexpr std::string_view kEdgeTarget = "edge";
constexpr std::string_view kNoGraphText = "No graph loaded: run editor_load <name> in the console.";
constexpr std::string_view kTrue = "true";
constexpr std::string_view kValueArgument = "value";

PanelAction ActionOf(std::string command, std::map<std::string, z13::gui::PanelArgument> arguments = {}) {
  return {.command = std::move(command),
          .tag = std::string(z13::editor::kEditorTagName),
          .arguments = std::move(arguments)};
}

PanelWidget Button(std::string label, std::string command, bool same_line) {
  return {.kind = PanelWidgetKind::kButton,
          .id = command,
          .label = std::move(label),
          .same_line = same_line,
          .action = ActionOf(std::move(command))};
}

std::string_view TargetName(EditorTarget target) {
  return target == EditorTarget::kEdge ? kEdgeTarget : kNodeTarget;
}

void AddList(z13::gui::Panel& panel, const z13::editor::EditorView& view, const std::vector<EditorItemView>& items,
             EditorTarget target, std::string_view heading) {
  panel.widgets.push_back({.kind = PanelWidgetKind::kHeading, .id = std::string(heading), .label = std::string(heading)});
  for (const EditorItemView& item : items) {
    const std::string target_name(TargetName(target));
    panel.widgets.push_back({.kind = PanelWidgetKind::kChoice,
                             .id = std::format("{}_{}", target_name, item.id),
                             .label = item.label,
                             .chosen = view.selected == z13::editor::EditorSelection {.target = target, .id = item.id},
                             .action = ActionOf("editor_select", {{"target", target_name}, {"id", item.id}})});
  }
}

void AddBox(z13::gui::Panel& panel, const EditorItemView& item) {
  struct Field {
    std::string_view argument;
    std::string_view label;
    int value;
  };
  const z13::editor::EditorBox& box = *item.box;
  const std::array fields {Field {"x", "X", box.origin.x()}, Field {"y", "Y", box.origin.y()},
                           Field {"z", "Z", box.origin.z()}, Field {"w", "Width", box.size.x()},
                           Field {"d", "Depth", box.size.y()}};
  for (const Field& field : fields) {
    PanelAction action = ActionOf("editor_set_room", {{"id", item.id}});
    for (const Field& other : fields) {
      action.arguments[std::string(other.argument)] = std::int64_t {other.value};
    }
    action.value_argument = std::string(field.argument);
    panel.widgets.push_back({.kind = PanelWidgetKind::kNumber,
                             .id = std::format("{}_box_{}", item.id, field.argument),
                             .label = std::string(field.label),
                             .value = field.value,
                             .action = std::move(action)});
  }
}

void AddProperty(z13::gui::Panel& panel, const EditorItemView& item, EditorTarget target,
                 const EditorPropertyView& property) {
  const std::string caption = property.unit.empty() ? property.title : std::format("{}, {}", property.title, property.unit);
  PanelAction action = ActionOf("editor_set_property", {{"target", std::string(TargetName(target))},
                                                        {"id", item.id},
                                                        {"name", property.name}});
  action.value_argument = std::string(kValueArgument);
  action.value_as_text = true;
  PanelWidget widget {.id = std::format("{}_{}_{}", TargetName(target), item.id, property.name),
                      .label = caption,
                      .action = std::move(action)};
  switch (property.kind) {
    case EditorPropertyKind::kInt:
      widget.kind = PanelWidgetKind::kNumber;
      widget.value = std::stoi(property.value);
      widget.min = property.min;
      widget.max = property.max;
      break;
    case EditorPropertyKind::kBool:
      widget.kind = PanelWidgetKind::kToggle;
      widget.chosen = property.value == kTrue;
      break;
    case EditorPropertyKind::kEnum:
      widget.kind = PanelWidgetKind::kPicker;
      widget.options = property.values;
      widget.current = property.value;
      break;
  }
  panel.widgets.push_back(std::move(widget));
}

void AddSelected(z13::gui::Panel& panel, const z13::editor::EditorView& view) {
  if (!view.selected) {
    return;
  }
  const EditorTarget target = view.selected->target;
  const auto& items = target == EditorTarget::kEdge ? view.edges : view.rooms;
  const auto item = std::ranges::find(items, view.selected->id, &EditorItemView::id);
  if (item == items.end()) {
    return;
  }
  panel.widgets.push_back({.kind = PanelWidgetKind::kHeading, .id = "selected", .label = item->label});
  if (!item->detail.empty()) {
    panel.widgets.push_back({.kind = PanelWidgetKind::kText, .id = "detail", .label = item->detail});
  }
  if (item->box) {
    AddBox(panel, *item);
  }
  for (const EditorPropertyView& property : item->properties) {
    AddProperty(panel, *item, target, property);
  }
}

}  // namespace

z13::gui::Panel BuildEditorPanel(const z13::editor::EditorView& view) {
  z13::gui::Panel panel {.id = std::string(kEditorPanelId),
                         .title = "Station editor",
                         .open = view.open,
                         .on_close = ActionOf("editor_window")};
  if (!view.loaded) {
    panel.widgets.push_back({.kind = PanelWidgetKind::kText, .id = "no_graph", .label = std::string(kNoGraphText)});
    return panel;
  }
  panel.widgets.push_back(Button("Undo", "editor_undo", false));
  panel.widgets.push_back(Button("Redo", "editor_redo", true));
  panel.widgets.push_back(Button("Pick aimed", "editor_pick", true));
  for (const std::string& problem : view.problems) {
    panel.widgets.push_back({.kind = PanelWidgetKind::kProblem, .id = problem, .label = problem});
  }
  AddList(panel, view, view.rooms, EditorTarget::kRoom, "Rooms");
  AddList(panel, view, view.edges, EditorTarget::kEdge, "Edges");
  AddSelected(panel, view);
  return panel;
}

}  // namespace z13::station_editor
