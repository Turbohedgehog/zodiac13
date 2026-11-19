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

#pragma once

#include <vector>

#include <flecs.h>

#include <z13/components/gui/gui_panels.h>

namespace z13::raylib::gui {

// Asks the action's command, logging a refusal like the console does.
void RunPanelAction(flecs::world world, const z13::gui::PanelAction& action);

// Draws every open panel the modules describe (z13/components/gui/gui_panels.h) and collects the actions its widgets
// ask; they run in a system outside the drawing one, which may not write entities. Singleton; never state.
class PanelWindows {
 public:
  using Singleton = void;

  void Draw(const z13::gui::GuiPanels& panels);

  std::vector<z13::gui::PanelAction> TakeActions();

 private:
  void DrawPanel(const z13::gui::Panel& panel);
  void DrawWidget(const z13::gui::PanelWidget& widget);
  void Ask(const z13::gui::PanelAction& action, const z13::gui::PanelArgument& value);

  std::vector<z13::gui::PanelAction> actions_;
};

}  // namespace z13::raylib::gui
