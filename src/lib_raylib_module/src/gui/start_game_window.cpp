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

#include "start_game_window.h"

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

#include <imgui.h>

#include <z13/components/gameplay.h>
#include <z13/components/net.h>

#include "gui_widgets.h"
#include "pending_transition.h"

namespace z13::raylib::gui {

namespace {

constexpr std::string_view kStartingGameLabel = "Starting the game...";

}  // namespace

StartGameWindow::StartGameWindow(flecs::world world) : Window(world, "Start Game"), scenes_(GameScenes(world.get<z13::Assets>().source)) {}

void StartGameWindow::DrawBody() {
  // Blueprint names are free text: ids by index keep them apart from each other and from Back.
  for (size_t i = 0; i < scenes_.size(); ++i) {
    ImGui::PushID(static_cast<int>(i));
    if (ImGui::Button(scenes_[i].label.c_str(), ButtonSize())) {
      Start(scenes_[i]);
    }
    ImGui::PopID();
  }
  if (ImGui::Button("Back", ButtonSize())) {
    RequestPop();
  }
}

void StartGameWindow::Start(const GameScene& scene) {
  World().set<PendingTransition>({.label = std::string(kStartingGameLabel), .action = [scene](flecs::world world) {
    world.set<z13::net::ConnectionStatus>({});
    SelectScene(world, scene);
    world.add<z13::gameplay::Gameplay>();
    world.remove<z13::gameplay::Pause>();
  }});
}

WindowPtr MakeStartGameWindow(flecs::world world) {
  return std::make_shared<StartGameWindow>(world);
}

}  // namespace z13::raylib::gui
