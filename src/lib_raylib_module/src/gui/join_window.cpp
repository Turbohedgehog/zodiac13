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
#include <string_view>

#include <imgui.h>
#include <imgui_stdlib.h>

#include <lib_core/settings/config.h>
#include <lib_core/utils/endpoint.h>

#include <z13/components/net.h>

#include "gui_widgets.h"

namespace z13::raylib::gui {

namespace {

constexpr std::string_view kDefaultJoinHost = "127.0.0.1";

}  // namespace

JoinWindow::JoinWindow(flecs::world world)
    : Window(world, "Join"), host_(kDefaultJoinHost), port_(std::to_string(z13::kDefaultServerPort)) {}

JoinWindow::StackRequest JoinWindow::OnBack() {
  CancelIfConnecting();
  return {StackOp::Pop, nullptr};
}

void JoinWindow::DrawBody() {
  const bool connecting = IsConnecting();
  ImGui::BeginDisabled(connecting);
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
