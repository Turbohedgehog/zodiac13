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

#include "key_bindings_window.h"

#include <memory>
#include <string>

#include <imgui.h>

#include "gui_widgets.h"

namespace z13::raylib::gui {

namespace {

constexpr float kBindingLabelWidthEm = 12.f;
constexpr float kBindingSlotWidthEm = 8.5f;
// Stride used to combine (group, action, slot) into a single ImGui PushID; must
// exceed the largest number of actions any group can have.
constexpr int kMaxActionsPerGroupForId = 64;

bool IsMouseButtonKeycode(z13::fbs::input::Keycode code) {
  return code >= z13::fbs::input::Keycode::MOUSE_BUTTON_LEFT &&
         code <= z13::fbs::input::Keycode::MOUSE_BUTTON_X2;
}

}  // namespace

KeyBindingsWindow::KeyBindingsWindow(flecs::world world)
    : Window(world, "Keyboard bindings"), model_(BuildKeyBindingModel(world)) {}

Window::StackRequest KeyBindingsWindow::OnBack() {
  if (rebind_.has_value()) {
    rebind_.reset();
    return {};
  }
  if (dirty_) {
    ApplyKeyBindingModel(World(), model_);
    dirty_ = false;
  }
  return {StackOp::Pop, nullptr};
}

void KeyBindingsWindow::OnKeyDown(z13::fbs::input::Keycode key_code) {
  if (!rebind_.has_value() || arm_countdown_ > 0) {
    return;
  }
  if (key_code == z13::fbs::input::Keycode::KEY_UNKNOWN || IsMouseButtonKeycode(key_code)) {
    return;
  }
  if (key_code != z13::fbs::input::Keycode::KEY_ESCAPE) {
    RebindSlot(model_, *rebind_, key_code);
    dirty_ = true;
  }
  rebind_.reset();
}

void KeyBindingsWindow::DrawBody() {
  if (rebind_.has_value()) {
    if (arm_countdown_ > 0) {
      --arm_countdown_;
    }
    ImGui::TextUnformatted("Press a key to bind, or Esc to cancel");
    return;
  }

  if (ImGui::BeginTabBar("KeyBindingGroups")) {
    for (int group = 0; group < static_cast<int>(model_.groups.size()); ++group) {
      const KeyBindingGroup& binding_group = model_.groups[group];
      // The rebind prompt replaces the whole body, so the tab bar is rebuilt after it.
      const ImGuiTabItemFlags flags =
          restore_tab_ && group == selected_group_ ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
      if (!ImGui::BeginTabItem(binding_group.name.c_str(), nullptr, flags)) {
        continue;
      }
      selected_group_ = group;
      DrawGroup(group, binding_group);
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
    restore_tab_ = false;
  }

  if (dirty_) {
    ImGui::Separator();
    if (ImGui::Button("Save changes", ButtonSize())) {
      ApplyKeyBindingModel(World(), model_);
      dirty_ = false;
    }
  }
}

void KeyBindingsWindow::DrawGroup(int group, const KeyBindingGroup& binding_group) {
  for (int action = 0; action < static_cast<int>(binding_group.actions.size()); ++action) {
    const KeyBindingAction& binding_action = binding_group.actions[action];
    ImGui::TextUnformatted(binding_action.display_text.c_str());
    ImGui::SameLine(Em(kBindingLabelWidthEm));
    for (int slot = 0; slot < kKeyBindingSlots; ++slot) {
      if (slot > 0) {
        ImGui::SameLine();
      }
      ImGui::PushID((group * kMaxActionsPerGroupForId + action) * kKeyBindingSlots + slot);
      const std::string slot_text(KeyBindingSlotText(model_, group, action, slot));
      if (ImGui::Button(slot_text.c_str(), {Em(kBindingSlotWidthEm), 0.f})) {
        rebind_ = KeyBindingSlot{group, action, slot};
        arm_countdown_ = 1;
        restore_tab_ = true;
      }
      ImGui::PopID();
    }
  }
}

WindowPtr MakeKeyBindings(flecs::world world) {
  return std::make_shared<KeyBindingsWindow>(world);
}

}  // namespace z13::raylib::gui
