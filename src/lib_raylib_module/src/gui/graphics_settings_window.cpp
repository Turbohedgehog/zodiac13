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

#include "graphics_settings_window.h"

#include <memory>

#include <imgui.h>

#include <lib_core/utils/log.h>
#include <z13_settings/environment.h>
#include <z13_settings/settings.h>
#include <z13_settings/settings_file.h>

#include "gui_widgets.h"

namespace z13::raylib::gui {

GraphicsSettingsWindow::GraphicsSettingsWindow(flecs::world world) : Window(world, "Graphics") {}

Window::StackRequest GraphicsSettingsWindow::OnBack() {
  SaveIfDirty();
  return {StackOp::Pop, nullptr};
}

void GraphicsSettingsWindow::DrawBody() {
  auto* tuning = World().try_get_mut<z13::RenderTuning>();
  if (tuning == nullptr) {
    DrawError("No render settings loaded");
    return;
  }
  // On means the schema's count, not this platform's default, which may be none.
  bool lamp_shadows = tuning->shadow_lights > 0;
  if (ImGui::Checkbox("Lamp shadows", &lamp_shadows)) {
    tuning->shadow_lights = lamp_shadows ? z13::SchemaDefaults<z13::fbs::settings::RenderTuningT>().shadow_lights : 0;
    dirty_ = true;
  }

  ImGui::Separator();
  if (ImGui::Button("Back", ButtonSize())) {
    SaveIfDirty();
    RequestPop();
  }
}

void GraphicsSettingsWindow::SaveIfDirty() {
  if (!dirty_) {
    return;
  }
  dirty_ = false;
  const auto path = z13::tools::environment::GetGameSettingsJsonPath();
  auto settings = z13::ReadSettings(path);
  if (!settings) {
    log_error("Graphics settings: not saved: {}", settings.error());
    return;
  }
  settings->render->shadow_lights = World().get<z13::RenderTuning>().shadow_lights;
  if (const auto written = z13::WriteSettings(path, *settings); !written) {
    log_error("Graphics settings: not saved: {}", written.error());
  }
}

WindowPtr MakeGraphicsSettings(flecs::world world) {
  return std::make_shared<GraphicsSettingsWindow>(world);
}

}  // namespace z13::raylib::gui
