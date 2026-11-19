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

#include "editor_window.h"

#include <algorithm>
#include <array>
#include <format>
#include <string_view>
#include <utility>
#include <variant>

#include <imgui.h>

#include <lib_core/commands/command_registry.h>
#include <lib_core/utils/log.h>
#include <lib_core/utils/flecs_utils.h>

#include "gui_widgets.h"

namespace z13::raylib::gui {

namespace {

using z13::editor::EditorItemView;
using z13::editor::EditorPropertyKind;
using z13::editor::EditorSelection;
using z13::editor::EditorTarget;
using z13::editor::EditorPropertyView;

constexpr std::string_view kTitle = "Station editor";
constexpr std::string_view kWindowCommand = "editor_window";
constexpr std::string_view kSetPropertyCommand = "editor_set_property";
constexpr std::string_view kSetRoomCommand = "editor_set_room";
constexpr std::string_view kSelectCommand = "editor_select";
constexpr std::string_view kPickCommand = "editor_pick";
constexpr std::string_view kUndoCommand = "editor_undo";
constexpr std::string_view kRedoCommand = "editor_redo";
constexpr std::string_view kNodeTarget = "node";
constexpr std::string_view kEdgeTarget = "edge";
constexpr std::string_view kTrue = "true";
constexpr std::string_view kFalse = "false";

constexpr float kWindowWidthEm = 26.f;
constexpr float kWindowHeightEm = 30.f;
constexpr float kRowHeightEm = 2.2f;
constexpr float kListHeightEm = 9.f;
constexpr float kFieldWidthEm = 10.f;
constexpr int kStep = 1;
constexpr int kFastStep = 5;

std::string Labeled(std::string_view title, std::string_view id) {
  return std::format("{}##{}", title, id);
}

const EditorItemView* Find(const std::vector<EditorItemView>& items, std::string_view id) {
  const auto found = std::ranges::find(items, id, &EditorItemView::id);
  return found != items.end() ? &*found : nullptr;
}

}  // namespace

void EditorWindow::Draw(const z13::editor::EditorView& view) {
  ImGui::SetNextWindowSize({Em(kWindowWidthEm), Em(kWindowHeightEm)}, ImGuiCond_FirstUseEver);
  bool kept = true;
  if (ImGui::Begin(kTitle.data(), &kept)) {
    if (!view.loaded) {
      ImGui::TextUnformatted("No graph loaded: run editor_load <name> in the console.");
    } else {
      if (ImGui::Button("Undo")) {
        Ask(std::string(kUndoCommand));
      }
      ImGui::SameLine();
      if (ImGui::Button("Redo")) {
        Ask(std::string(kRedoCommand));
      }
      ImGui::SameLine();
      if (ImGui::Button("Pick aimed")) {
        Ask(std::string(kPickCommand));
      }
      for (const std::string& problem : view.problems) {
        ImGui::TextColored(kErrorColor, "%s", problem.c_str());
      }
      DrawList(view);
      DrawProperties(view);
    }
  }
  ImGui::End();
  if (!kept) {
    Ask(std::string(kWindowCommand));
  }
}

std::vector<EditorRequest> EditorWindow::TakeRequests() {
  return std::exchange(requests_, {});
}

void EditorWindow::DrawList(const z13::editor::EditorView& view) {
  if (ImGui::BeginChild("##editor_list", {0.f, Em(kListHeightEm)}, ImGuiChildFlags_Borders)) {
    const auto draw = [this, &view](const std::vector<EditorItemView>& items, EditorTarget target,
                                    std::string_view heading) {
      ImGui::TextUnformatted(heading.data());
      for (const EditorItemView& item : items) {
        const EditorSelection entry {.target = target, .id = item.id};
        const std::string_view prefix = target == EditorTarget::kEdge ? kEdgeTarget : kNodeTarget;
        if (ImGui::Selectable(Labeled(item.label, std::format("{}_{}", prefix, item.id)).c_str(),
                              view.selected == entry, ImGuiSelectableFlags_None, {0.f, Em(kRowHeightEm)})) {
          Ask(std::string(kSelectCommand), {{"target", std::string(prefix)}, {"id", item.id}});
        }
      }
    };
    draw(view.rooms, EditorTarget::kRoom, "Rooms");
    draw(view.edges, EditorTarget::kEdge, "Edges");
  }
  ImGui::EndChild();
}

void EditorWindow::DrawProperties(const z13::editor::EditorView& view) {
  if (!view.selected) {
    return;
  }
  const bool edge = view.selected->target == EditorTarget::kEdge;
  const EditorItemView* item = Find(edge ? view.edges : view.rooms, view.selected->id);
  if (item == nullptr) {
    return;
  }
  ImGui::SeparatorText(item->label.c_str());
  if (!item->detail.empty()) {
    ImGui::TextUnformatted(item->detail.c_str());
  }
  if (item->box) {
    DrawBox(*item);
  }
  for (const EditorPropertyView& property : item->properties) {
    DrawProperty(*item, edge, property);
  }
}

void EditorWindow::DrawBox(const EditorItemView& item) {
  struct Field {
    std::string_view label;
    int value;
  };
  const z13::editor::EditorBox& box = *item.box;
  std::array fields {Field {"X", box.origin.x()}, Field {"Y", box.origin.y()}, Field {"Z", box.origin.z()},
                     Field {"Width", box.size.x()}, Field {"Depth", box.size.y()}};
  bool changed = false;
  for (Field& field : fields) {
    ImGui::SetNextItemWidth(Em(kFieldWidthEm));
    changed |= ImGui::InputInt(Labeled(field.label, item.id + "_box_" + std::string(field.label)).c_str(), &field.value,
                               kStep, kFastStep, ImGuiInputTextFlags_EnterReturnsTrue);
  }
  if (changed) {
    Ask(std::string(kSetRoomCommand), {{"id", item.id},
                                       {"x", std::int64_t {fields[0].value}},
                                       {"y", std::int64_t {fields[1].value}},
                                       {"z", std::int64_t {fields[2].value}},
                                       {"w", std::int64_t {fields[3].value}},
                                       {"d", std::int64_t {fields[4].value}}});
  }
}

void EditorWindow::DrawProperty(const EditorItemView& item, bool edge, const EditorPropertyView& property) {
  const std::string label = Labeled(property.unit.empty() ? property.title : std::format("{}, {}", property.title, property.unit),
                                    item.id + "_" + property.name);
  const auto set = [this, &item, &property, edge](std::string_view value) {
    Ask(std::string(kSetPropertyCommand), {{"target", std::string(edge ? kEdgeTarget : kNodeTarget)},
                                           {"id", item.id},
                                           {"name", property.name},
                                           {"value", std::string(value)}});
  };
  ImGui::SetNextItemWidth(Em(kFieldWidthEm));
  switch (property.kind) {
    case EditorPropertyKind::kInt: {
      int number = std::stoi(property.value);
      if (ImGui::InputInt(label.c_str(), &number, kStep, kFastStep, ImGuiInputTextFlags_EnterReturnsTrue)) {
        set(std::to_string(std::clamp(number, property.min, property.max)));
      }
      break;
    }
    case EditorPropertyKind::kBool: {
      bool flag = property.value == kTrue;
      if (ImGui::Checkbox(label.c_str(), &flag)) {
        set(flag ? kTrue : kFalse);
      }
      break;
    }
    case EditorPropertyKind::kEnum:
      if (ImGui::BeginCombo(label.c_str(), property.value.c_str())) {
        for (const std::string& value : property.values) {
          if (ImGui::Selectable(value.c_str(), value == property.value)) {
            set(value);
          }
        }
        ImGui::EndCombo();
      }
      break;
  }
}

void RunEditorRequest(flecs::world world, const EditorRequest& request) {
  rfl::Generic::Object arguments;
  for (const auto& [name, value] : request.arguments) {
    arguments[name] = std::visit([](const auto& argument) { return rfl::Generic(argument); }, value);
  }
  const auto reply = [](z13::CommandResult result) {
    if (!result) {
      log_error("{}", result.error());
    }
  };
  const z13::ImmediateScope immediate(world);
  const z13::Status submitted = z13::SubmitCommand(
      world, {.tags = {std::string(z13::editor::kEditorTagName)}}, request.command, rfl::Generic(std::move(arguments)), reply);
  if (!submitted) {
    log_error("{}", submitted.error());
  }
}

void EditorWindow::Ask(std::string command, std::map<std::string, EditorArgument> arguments) {
  requests_.push_back({.command = std::move(command), .arguments = std::move(arguments)});
}

}  // namespace z13::raylib::gui
