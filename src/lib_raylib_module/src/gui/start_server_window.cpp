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

#include "start_server_window.h"

#include <filesystem>
#include <memory>
#include <string_view>

#include <imgui.h>
#include <imgui_stdlib.h>

#include <lib_core/settings/config.h>
#include <lib_core/utils/endpoint.h>

#include <z13/components/net.h>
#include <z13/components/station.h>
#include <z13_primitives/station_assets.h>

#include "../tools/asset_path.h"
#include "gui_widgets.h"

namespace z13::raylib::gui {

namespace {

constexpr std::string_view kEmptyScene = "(new station)";

}  // namespace

StartServerWindow::StartServerWindow(flecs::world world)
    : Window(world, "Start Server"),
      port_(std::to_string(z13::kDefaultServerPort)),
      station_scenes_(z13::building::primitives::BlueprintScenes(AssetPath(std::filesystem::path {}))) {}

void StartServerWindow::DrawBody() {
  ImGui::SetNextItemWidth(kAddressFieldWidth);
  ImGui::InputText("Port", &port_, ImGuiInputTextFlags_CharsDecimal);
  ImGui::Checkbox("Station", &station_);
  if (station_) {
    DrawSceneCombo();
  }

  const auto& status = World().get<z13::net::ConnectionStatus>();
  const auto port = z13::ParsePort(port_);
  if (!port) {
    DrawError(port.error());
  } else if (submitted_ && status.state == z13::net::ConnectionState::kFailed) {
    DrawError(status.reason);
  }

  ImGui::BeginDisabled(!port);
  if (ImGui::Button("Start", kButtonSize)) {
    if (station_) {
      World().add<z13::station::StationMode>();
      World().set(z13::station::StationSceneChoice {
          .scene = scene_ ? std::optional<std::string>(station_scenes_[*scene_]) : std::nullopt});
    } else {
      World().remove<z13::station::StationMode>();
    }
    World().entity().set<z13::net::StartServerRequest>({.port = *port});
    submitted_ = true;
  }
  ImGui::EndDisabled();
  if (ImGui::Button("Back", kButtonSize)) {
    RequestPop();
  }
}

void StartServerWindow::DrawSceneCombo() {
  if (!ImGui::BeginCombo("Scene", scene_ ? station_scenes_[*scene_].c_str() : kEmptyScene.data())) {
    return;
  }
  if (ImGui::Selectable(kEmptyScene.data(), !scene_)) {
    scene_ = std::nullopt;
  }
  for (size_t i = 0; i < station_scenes_.size(); ++i) {
    if (ImGui::Selectable(station_scenes_[i].c_str(), scene_ == i)) {
      scene_ = i;
    }
  }
  ImGui::EndCombo();
}

WindowPtr MakeStartServerWindow(flecs::world world) {
  return std::make_shared<StartServerWindow>(world);
}

}  // namespace z13::raylib::gui
