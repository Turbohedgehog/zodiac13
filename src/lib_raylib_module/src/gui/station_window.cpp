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

#include "station_window.h"

#include <filesystem>
#include <memory>
#include <utility>

#include <imgui.h>

#include <z13/components/gameplay.h>
#include <z13/components/net.h>
#include <z13/components/station.h>
#include <z13_primitives/station_assets.h>

#include "../tools/asset_path.h"
#include "gui_widgets.h"

namespace z13::raylib::gui {

StationWindow::StationWindow(flecs::world world)
    : Window(world, "Station"),
      scenes_(z13::building::primitives::BlueprintScenes(AssetPath(std::filesystem::path {}))) {}

void StationWindow::DrawBody() {
  for (const std::string& scene : scenes_) {
    if (ImGui::Button(scene.c_str(), kButtonSize)) {
      Start(scene);
    }
  }
  if (ImGui::Button("New station", kButtonSize)) {
    Start(std::nullopt);
  }
  if (ImGui::Button("Back", kButtonSize)) {
    RequestPop();
  }
}

void StationWindow::Start(std::optional<std::string> scene) {
  World().set<z13::net::ConnectionStatus>({});
  World().add<z13::station::StationMode>();
  World().set(z13::station::StationSceneChoice {.scene = std::move(scene)});
  World().add<z13::gameplay::Gameplay>();
  RequestCloseMenu();
}

WindowPtr MakeStationWindow(flecs::world world) {
  return std::make_shared<StationWindow>(world);
}

}  // namespace z13::raylib::gui
