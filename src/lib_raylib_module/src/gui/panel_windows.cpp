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

#include "panel_windows.h"

#include <algorithm>
#include <format>
#include <string>
#include <string_view>
#include <utility>

#include <imgui.h>

#include <lib_core/commands/command_registry.h>
#include <lib_core/utils/flecs_utils.h>
#include <lib_core/utils/log.h>

#include "gui_widgets.h"

namespace z13::raylib::gui {

namespace {

using z13::gui::PanelAction;
using z13::gui::PanelArgument;
using z13::gui::PanelWidget;
using z13::gui::PanelWidgetKind;

constexpr float kWindowWidthEm = 26.f;
constexpr float kWindowHeightEm = 30.f;
constexpr float kRowHeightEm = 2.2f;
constexpr float kFieldWidthEm = 10.f;
constexpr int kStep = 1;
constexpr int kFastStep = 5;
constexpr std::string_view kTrue = "true";
constexpr std::string_view kFalse = "false";

std::string Labeled(std::string_view caption, std::string_view id) {
  return std::format("{}##{}", caption, id);
}

}  // namespace

void RunPanelAction(flecs::world world, const PanelAction& action) {
  rfl::Generic::Object arguments;
  for (const auto& [name, value] : action.arguments) {
    arguments[name] = std::visit([](const auto& argument) { return rfl::Generic(argument); }, value);
  }
  const auto reply = [](z13::CommandResult result) {
    if (!result) {
      log_error("{}", result.error());
    }
  };
  const z13::ImmediateScope immediate(world);
  const z13::Status submitted = z13::SubmitCommand(world, {.tags = {action.tag}}, action.command,
                                                   rfl::Generic(std::move(arguments)), reply);
  if (!submitted) {
    log_error("{}", submitted.error());
  }
}

void PanelWindows::Draw(const z13::gui::GuiPanels& panels) {
  for (const z13::gui::Panel& panel : panels.panels) {
    if (panel.open) {
      DrawPanel(panel);
    }
  }
}

std::vector<PanelAction> PanelWindows::TakeActions() {
  return std::exchange(actions_, {});
}

void PanelWindows::DrawPanel(const z13::gui::Panel& panel) {
  ImGui::SetNextWindowSize({Em(kWindowWidthEm), Em(kWindowHeightEm)}, ImGuiCond_FirstUseEver);
  bool kept = true;
  if (ImGui::Begin(Labeled(panel.title, panel.id).c_str(), &kept)) {
    for (const PanelWidget& widget : panel.widgets) {
      if (widget.same_line) {
        ImGui::SameLine();
      }
      DrawWidget(widget);
    }
  }
  ImGui::End();
  if (!kept && !panel.on_close.command.empty()) {
    actions_.push_back(panel.on_close);
  }
}

void PanelWindows::DrawWidget(const PanelWidget& widget) {
  const std::string label = Labeled(widget.label, widget.id);
  switch (widget.kind) {
    case PanelWidgetKind::kHeading:
      ImGui::SeparatorText(widget.label.c_str());
      break;
    case PanelWidgetKind::kText:
      ImGui::TextUnformatted(widget.label.c_str());
      break;
    case PanelWidgetKind::kProblem:
      ImGui::TextColored(kErrorColor, "%s", widget.label.c_str());
      break;
    case PanelWidgetKind::kButton:
      if (ImGui::Button(label.c_str())) {
        Ask(widget.action, {});
      }
      break;
    case PanelWidgetKind::kChoice:
      if (ImGui::Selectable(label.c_str(), widget.chosen, ImGuiSelectableFlags_None, {0.f, Em(kRowHeightEm)})) {
        Ask(widget.action, {});
      }
      break;
    case PanelWidgetKind::kNumber: {
      int number = widget.value;
      ImGui::SetNextItemWidth(Em(kFieldWidthEm));
      if (ImGui::InputInt(label.c_str(), &number, kStep, kFastStep, ImGuiInputTextFlags_EnterReturnsTrue)) {
        Ask(widget.action, std::int64_t {widget.min < widget.max ? std::clamp(number, widget.min, widget.max) : number});
      }
      break;
    }
    case PanelWidgetKind::kToggle: {
      bool on = widget.chosen;
      if (ImGui::Checkbox(label.c_str(), &on)) {
        Ask(widget.action, std::string(on ? kTrue : kFalse));
      }
      break;
    }
    case PanelWidgetKind::kPicker:
      ImGui::SetNextItemWidth(Em(kFieldWidthEm));
      if (ImGui::BeginCombo(label.c_str(), widget.current.c_str())) {
        for (const std::string& option : widget.options) {
          if (ImGui::Selectable(option.c_str(), option == widget.current)) {
            Ask(widget.action, option);
          }
        }
        ImGui::EndCombo();
      }
      break;
  }
}

void PanelWindows::Ask(const PanelAction& action, const PanelArgument& value) {
  PanelAction asked = action;
  if (!action.value_argument.empty()) {
    asked.arguments[action.value_argument] = action.value_as_text ? PanelArgument {std::visit(
                                                                        [](const auto& shown) {
                                                                          return std::format("{}", shown);
                                                                        },
                                                                        value)}
                                                                  : value;
  }
  actions_.push_back(std::move(asked));
}

}  // namespace z13::raylib::gui
