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

#include "net_windows.h"

#include <format>
#include <memory>
#include <string>
#include <string_view>

#include <imgui.h>
#include <imgui_stdlib.h>

#include <lib_core/settings/config.h>
#include <lib_core/utils/endpoint.h>

#include <z13/components/net.h>

#include "gui_widgets.h"

namespace z13::raylib::gui {

namespace {

constexpr float kAddressFieldWidth = 200.f;
constexpr std::string_view kDefaultJoinHost = "127.0.0.1";

const z13::net::ConnectionStatus& Status(flecs::world world) {
  return world.get<z13::net::ConnectionStatus>();
}

// Success removes Pause, which closes the menu; only a failure is left to show here.
class StartServerWindow : public Window {
 public:
  explicit StartServerWindow(flecs::world world)
      : Window(world, "Start Server"), port_(std::to_string(z13::kDefaultServerPort)) {}

 protected:
  void DrawBody() override {
    ImGui::SetNextItemWidth(kAddressFieldWidth);
    ImGui::InputText("Port", &port_, ImGuiInputTextFlags_CharsDecimal);

    const auto port = z13::ParsePort(port_);
    if (!port) {
      DrawError(port.error());
    } else if (submitted_ && Status(World()).state == z13::net::ConnectionState::kFailed) {
      DrawError(Status(World()).reason);
    }

    ImGui::BeginDisabled(!port);
    if (ImGui::Button("Start", kButtonSize)) {
      World().entity().set<z13::net::StartServerRequest>({.port = *port});
      submitted_ = true;
    }
    ImGui::EndDisabled();
    if (ImGui::Button("Back", kButtonSize)) {
      RequestPop();
    }
  }

 private:
  std::string port_;
  bool submitted_ {};
};

class JoinWindow : public Window {
 public:
  explicit JoinWindow(flecs::world world)
      : Window(world, "Join"), host_(kDefaultJoinHost), port_(std::to_string(z13::kDefaultServerPort)) {}

  StackRequest OnBack() override {
    CancelIfConnecting();
    return {StackOp::Pop, nullptr};
  }

 protected:
  void DrawBody() override {
    const bool connecting = IsConnecting();
    ImGui::BeginDisabled(connecting);
    ImGui::SetNextItemWidth(kAddressFieldWidth);
    ImGui::InputText("Host", &host_);
    ImGui::SetNextItemWidth(kAddressFieldWidth);
    ImGui::InputText("Port", &port_, ImGuiInputTextFlags_CharsDecimal);
    ImGui::EndDisabled();

    const auto endpoint = z13::ParseEndpoint(std::format("{}:{}", host_, port_), z13::kDefaultServerPort);
    if (connecting) {
      ImGui::TextUnformatted("Connecting...");
    } else if (!endpoint) {
      DrawError(endpoint.error());
    } else if (submitted_ && Status(World()).state == z13::net::ConnectionState::kFailed) {
      DrawError(Status(World()).reason);
    }

    ImGui::BeginDisabled(connecting || !endpoint);
    if (ImGui::Button("Connect", kButtonSize)) {
      World().entity().set<z13::net::JoinRequest>({.endpoint = *endpoint});
      submitted_ = true;
    }
    ImGui::EndDisabled();
    if (ImGui::Button(connecting ? "Cancel" : "Back", kButtonSize)) {
      CancelIfConnecting();
      RequestPop();
    }
  }

 private:
  bool IsConnecting() const {
    return submitted_ && Status(World()).state == z13::net::ConnectionState::kConnecting;
  }

  void CancelIfConnecting() {
    if (IsConnecting()) {
      World().entity().add<z13::net::LeaveRequest>();
    }
  }

  std::string host_;
  std::string port_;
  bool submitted_ {};
};

}  // namespace

WindowPtr MakeStartServerWindow(flecs::world world) {
  return std::make_shared<StartServerWindow>(world);
}

WindowPtr MakeJoinWindow(flecs::world world) {
  return std::make_shared<JoinWindow>(world);
}

}  // namespace z13::raylib::gui
