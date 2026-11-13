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

#include "pause_menu_window.h"

#include <memory>

#include <imgui.h>

#include <z13/components/gameplay/gameplay.h>

#include "game_transitions.h"
#include "graphics_settings_window.h"
#include "gui_widgets.h"
#include "input_settings_window.h"
#include "profile_window.h"
#include "render_diagnostics.h"

namespace z13::raylib::gui {

GameplayPauseMenuWindow::GameplayPauseMenuWindow(flecs::world world) : Window(world, "Paused") {}

Window::StackRequest GameplayPauseMenuWindow::OnBack() {
  return {StackOp::CloseMenu, nullptr};
}

void GameplayPauseMenuWindow::DrawBody() {
  if (ImGui::Button("Resume", ButtonSize())) {
    RequestCloseMenu();
  }
  if (ImGui::Button("Controls...", ButtonSize())) {
    RequestPush(MakeInputSettings(World()));
  }
  if (ImGui::Button("Graphics...", ButtonSize())) {
    RequestPush(MakeGraphicsSettings(World()));
  }
  if (ImGui::Button("Profile...", ButtonSize())) {
    RequestPush(MakeProfileWindow(World()));
  }
  if (ImGui::Button("Exit to Main Menu", ButtonSize())) {
    LeaveGame(World());
  }
  DrawRenderDiagnostics(World());
}

WindowPtr MakeGameplayPauseMenu(flecs::world world) {
  return std::make_shared<GameplayPauseMenuWindow>(world);
}

}  // namespace z13::raylib::gui
