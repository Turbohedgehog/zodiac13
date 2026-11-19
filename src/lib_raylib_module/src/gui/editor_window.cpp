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
#include <format>
#include <string_view>
#include <utility>

#include <imgui.h>

#include <lib_core/commands/command_registry.h>
#include <lib_core/utils/log.h>
#include <lib_core/utils/flecs_utils.h>

#include "gui_widgets.h"

namespace z13::raylib::gui {

namespace {

using z13::editor::EditorItemView;
using z13::editor::EditorPropertyKind;
using z13::editor::EditorPropertyView;

constexpr std::string_view kTitle = "Station editor";
constexpr std::string_view kWindowCommand = "editor_window";
constexpr std::string_view kSetPropertyCommand = "editor_set_property";
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
    const auto draw = [this](const std::vector<EditorItemView>& items, bool edge, std::string_view heading) {
      ImGui::TextUnformatted(heading.data());
      for (const EditorItemView& item : items) {
        const EditorSelection entry {.edge = edge, .id = item.id};
        if (ImGui::Selectable(Labeled(item.label, edge ? "edge_" + item.id : "room_" + item.id).c_str(),
                              selection_ == entry, ImGuiSelectableFlags_None, {0.f, Em(kRowHeightEm)})) {
          selection_ = entry;
        }
      }
    };
    draw(view.rooms, false, "Rooms");
    draw(view.edges, true, "Edges");
  }
  ImGui::EndChild();
}

void EditorWindow::DrawProperties(const z13::editor::EditorView& view) {
  if (!selection_) {
    return;
  }
  const EditorItemView* item = Find(selection_->edge ? view.edges : view.rooms, selection_->id);
  if (item == nullptr) {
    selection_.reset();
    return;
  }
  ImGui::SeparatorText(item->label.c_str());
  if (!item->detail.empty()) {
    ImGui::TextUnformatted(item->detail.c_str());
  }
  for (const EditorPropertyView& property : item->properties) {
    DrawProperty(*item, property);
  }
}

void EditorWindow::DrawProperty(const EditorItemView& item, const EditorPropertyView& property) {
  const std::string label = Labeled(property.unit.empty() ? property.title : std::format("{}, {}", property.title, property.unit),
                                    item.id + "_" + property.name);
  const auto set = [this, &item, &property](std::string_view value) {
    Ask(std::string(kSetPropertyCommand), {{"target", std::string(selection_->edge ? kEdgeTarget : kNodeTarget)},
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
    arguments[name] = rfl::Generic(value);
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

void EditorWindow::Ask(std::string command, std::map<std::string, std::string> arguments) {
  requests_.push_back({.command = std::move(command), .arguments = std::move(arguments)});
}

}  // namespace z13::raylib::gui
