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

#include <memory>
#include <string>

#include <imgui.h>
#include <imgui_stdlib.h>

#include <lib_core/settings/config.h>
#include <lib_core/utils/endpoint.h>

#include <z13/components/net.h>

#include "gui_widgets.h"

namespace z13::raylib::gui {

StartServerWindow::StartServerWindow(flecs::world world)
    : Window(world, "Start Server"),
      port_(std::to_string(z13::kDefaultServerPort)),
      scenes_(GameScenes()) {}

void StartServerWindow::DrawBody() {
  ImGui::SetNextItemWidth(AddressFieldWidth());
  ImGui::InputText("Port", &port_, ImGuiInputTextFlags_CharsDecimal);
  DrawSceneCombo();

  const auto& status = World().get<z13::net::ConnectionStatus>();
  const auto port = z13::ParsePort(port_);
  if (!port) {
    DrawError(port.error());
  } else if (submitted_ && status.state == z13::net::ConnectionState::kFailed) {
    DrawError(status.reason);
  }

  ImGui::BeginDisabled(!port);
  if (ImGui::Button("Start", ButtonSize())) {
    SelectScene(World(), scenes_[scene_]);
    World().entity().set<z13::net::StartServerRequest>({.port = *port});
    submitted_ = true;
  }
  ImGui::EndDisabled();
  if (ImGui::Button("Back", ButtonSize())) {
    RequestPop();
  }
}

void StartServerWindow::DrawSceneCombo() {
  ImGui::SetNextItemWidth(AddressFieldWidth());
  if (!ImGui::BeginCombo("Scene", scenes_[scene_].label.c_str())) {
    return;
  }
  for (size_t i = 0; i < scenes_.size(); ++i) {
    ImGui::PushID(static_cast<int>(i));
    if (ImGui::Selectable(scenes_[i].label.c_str(), scene_ == i)) {
      scene_ = i;
    }
    ImGui::PopID();
  }
  ImGui::EndCombo();
}

WindowPtr MakeStartServerWindow(flecs::world world) {
  return std::make_shared<StartServerWindow>(world);
}

}  // namespace z13::raylib::gui
