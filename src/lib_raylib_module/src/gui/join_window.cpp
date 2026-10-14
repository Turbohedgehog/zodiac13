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

#include "join_window.h"

#include <format>
#include <memory>
#include <string>
#include <string_view>

#include <imgui.h>
#include <imgui_stdlib.h>

#include <lib_core/settings/config.h>
#include <lib_core/utils/endpoint.h>

#include <z13/components/net.h>

#include "game_scenes.h"
#include "gui_widgets.h"

namespace z13::raylib::gui {

namespace {

constexpr std::string_view kDefaultJoinHost = "127.0.0.1";

}  // namespace

JoinWindow::JoinWindow(flecs::world world)
    : Window(world, "Join"), host_(kDefaultJoinHost), port_(std::to_string(z13::kDefaultServerPort)) {
  World().add<z13::net::LanSearch>();
}

JoinWindow::~JoinWindow() {
  World().remove<z13::net::LanSearch>();
}

JoinWindow::StackRequest JoinWindow::OnBack() {
  CancelIfConnecting();
  return {StackOp::Pop, nullptr};
}

void JoinWindow::DrawFoundServers() {
  const auto& found = World().get<z13::net::FoundServers>().servers;
  if (found.empty()) {
    ImGui::TextDisabled("Looking for servers on the LAN...");
    return;
  }
  for (const z13::net::FoundServer& server : found) {
    const std::string label = std::format("{}  {}:{}  {}  players {}  {} ms", server.name, server.endpoint.host,
                                          server.endpoint.port, server.scene.value_or(std::string(kSpaceshipLabel)),
                                          server.players, server.ping_ms);
    ImGui::BeginDisabled(server.incompatible.has_value());
    if (ImGui::Selectable(label.c_str(), host_ == server.endpoint.host && port_ == std::to_string(server.endpoint.port))) {
      host_ = server.endpoint.host;
      port_ = std::to_string(server.endpoint.port);
    }
    ImGui::EndDisabled();
    if (server.incompatible) {
      ImGui::TextDisabled("  %s", server.incompatible->c_str());
    }
  }
}

void JoinWindow::DrawBody() {
  const bool connecting = IsConnecting();
  ImGui::BeginDisabled(connecting);
  DrawFoundServers();
  ImGui::Separator();
  ImGui::SetNextItemWidth(AddressFieldWidth());
  ImGui::InputText("Host", &host_);
  ImGui::SetNextItemWidth(AddressFieldWidth());
  ImGui::InputText("Port", &port_, ImGuiInputTextFlags_CharsDecimal);
  ImGui::EndDisabled();

  const auto& status = World().get<z13::net::ConnectionStatus>();
  const auto endpoint = z13::ParseEndpoint(std::format("{}:{}", host_, port_), z13::kDefaultServerPort);
  if (connecting) {
    ImGui::TextUnformatted("Connecting...");
  } else if (!endpoint) {
    DrawError(endpoint.error());
  } else if (submitted_ && status.state == z13::net::ConnectionState::kFailed) {
    DrawError(status.reason);
  }

  ImGui::BeginDisabled(connecting || !endpoint);
  if (ImGui::Button("Connect", ButtonSize())) {
    World().entity().set<z13::net::JoinRequest>({.endpoint = *endpoint});
    submitted_ = true;
  }
  ImGui::EndDisabled();
  if (ImGui::Button(connecting ? "Cancel" : "Back", ButtonSize())) {
    CancelIfConnecting();
    RequestPop();
  }
}

bool JoinWindow::IsConnecting() const {
  return submitted_ && World().get<z13::net::ConnectionStatus>().state == z13::net::ConnectionState::kConnecting;
}

void JoinWindow::CancelIfConnecting() {
  if (IsConnecting()) {
    World().entity().add<z13::net::LeaveRequest>();
  }
}

WindowPtr MakeJoinWindow(flecs::world world) {
  return std::make_shared<JoinWindow>(world);
}

}  // namespace z13::raylib::gui
