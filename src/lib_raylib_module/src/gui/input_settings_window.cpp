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

#include "input_settings_window.h"

#include <memory>

#include <imgui.h>

#include <z13/components/input.h>

#include "gui_widgets.h"
#include "key_bindings_window.h"

namespace z13::raylib::gui {

InputSettingsWindow::InputSettingsWindow(flecs::world world) : Window(world, "Settings") {}

Window::StackRequest InputSettingsWindow::OnBack() {
  SaveIfDirty();
  return {StackOp::Pop, nullptr};
}

void InputSettingsWindow::DrawBody() {
  auto& config = World().ensure<z13::input::InputConfig>();

  if (ImGui::Checkbox("Invert X", &config.invert_x)) {
    dirty_ = true;
  }
  if (ImGui::Checkbox("Invert Y", &config.invert_y)) {
    dirty_ = true;
  }
  if (ImGui::SliderFloat("Sensitivity", &config.mouse_sensitivity, 0.f, 10.f, "%.1f")) {
    dirty_ = true;
  }

  ImGui::Separator();
  if (ImGui::Button("Keyboard bindings...", kButtonSize)) {
    RequestPush(MakeKeyBindings(World()));
  }
  if (ImGui::Button("Back", kButtonSize)) {
    SaveIfDirty();
    RequestPop();
  }
}

void InputSettingsWindow::SaveIfDirty() {
  if (!dirty_) {
    return;
  }
  dirty_ = false;
  flecs::world world = World();
  world.modified<z13::input::InputConfig>();
  world.event<z13::input::SystemInputEventType>()
      .id<z13::input::SaveConfigEvent>()
      .entity(world.entity().add<z13::input::SaveConfigEvent>())
      .enqueue();
}

WindowPtr MakeInputSettings(flecs::world world) {
  return std::make_shared<InputSettingsWindow>(world);
}

}  // namespace z13::raylib::gui
