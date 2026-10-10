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

#include "main_menu_window.h"

#include <memory>

#include <imgui.h>

#include <z13/components/net.h>

#include <raylib_module/raylib_components.h>

#include "gui_widgets.h"
#include "input_settings_window.h"
#include "join_window.h"
#include "start_game_window.h"
#include "start_server_window.h"

namespace z13::raylib::gui {

MainMenuWindow::MainMenuWindow(flecs::world world) : Window(world, "Main menu") {}

Window::StackRequest MainMenuWindow::OnBack() {
  return {};
}

void MainMenuWindow::DrawBody() {
  const auto& status = World().get<z13::net::ConnectionStatus>();
  if (status.state == z13::net::ConnectionState::kConnecting) {  // --connect
    ImGui::TextUnformatted("Connecting...");
    if (ImGui::Button("Cancel", ButtonSize())) {
      World().entity().add<z13::net::LeaveRequest>();
    }
    return;
  }
  // Why the last session ended or failed, e.g. the server went away.
  if (status.state == z13::net::ConnectionState::kFailed ||
      status.state == z13::net::ConnectionState::kDisconnected) {
    DrawError(status.reason);
  }

  if (ImGui::Button("Start Game...", ButtonSize())) {
    RequestPush(MakeStartGameWindow(World()));
  }
  if (ImGui::Button("Start Server...", ButtonSize())) {
    RequestPush(MakeStartServerWindow(World()));
  }
  if (ImGui::Button("Join...", ButtonSize())) {
    RequestPush(MakeJoinWindow(World()));
  }
  if (ImGui::Button("Settings...", ButtonSize())) {
    RequestPush(MakeInputSettings(World()));
  }
  if (ImGui::Button("Exit", ButtonSize())) {
    World().add<RaylibWindowClosed>();
  }
}

WindowPtr MakeMainMenu(flecs::world world) {
  return std::make_shared<MainMenuWindow>(world);
}

}  // namespace z13::raylib::gui
