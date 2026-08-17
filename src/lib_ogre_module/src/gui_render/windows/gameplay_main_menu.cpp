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

module;

#include "gameplay_main_menu.h"

#include <imgui.h>

#include "window_component.h"
#include "window_factory.h"

module z13.ogre.gui;

import z13.ogre.components;

namespace z13::ogre::gui {

GameplayMainMenuWindow::GameplayMainMenuWindow(flecs::world world)
  : WindowBase(world, "Main menu") {}


void GameplayMainMenuWindow::DrawImpl() {
  auto world = GetWorld();
  if (ImGui::Button("Back", ImVec2(120, 0))) {
    OnBackEvent();
  }

  if (ImGui::Button("Settings...", ImVec2(120, 0))) {
    auto& window_component = world.ensure<z13::ogre::gui::WindowComponent>();
    window_component.modal_window_stack.emplace(WindowFactory::CreateInputSettingsMenu(world));
  }

  if (ImGui::Button("Exit", ImVec2(120, 0))) {
    world.add<OgreWindowClosed>();
  }
}

void GameplayMainMenuWindow::OnBackEvent() {
  GetWorld().remove_all<gameplay::Pause>();
  WindowBase::OnBackEvent();
}

}  // namespace z13::ogre::gui
