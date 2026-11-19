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

#include "layer_button.h"

#include <string>
#include <string_view>

#include <imgui.h>

#include "gui_widgets.h"

namespace z13::raylib::gui {

namespace {

constexpr float kButtonWidthEm = 6.f;
constexpr float kButtonHeightEm = 3.f;
constexpr float kMarginEm = 0.75f;
constexpr std::string_view kWindowName = "##layer_button";
constexpr std::string_view kIdleLabel = "Layers";

}  // namespace

Eigen::AlignedBox2f DrawLayerButton(const z13::station::OverlayLayers& layers) {
  const ImVec2 display = ImGui::GetIO().DisplaySize;
  const ImVec2 size {Em(kButtonWidthEm), Em(kButtonHeightEm)};
  const float margin = Em(kMarginEm);
  const ImVec2 position {display.x - size.x - margin, margin * 2.f + Em(kButtonHeightEm)};  // under the console's button, as tall
  ImGui::SetNextWindowPos(position);
  ImGui::SetNextWindowSize(size);
  ImGui::SetNextWindowBgAlpha(0.f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.f, 0.f});
  constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                      ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs;
  if (ImGui::Begin(kWindowName.data(), nullptr, kFlags)) {
    // NoInputs: the tap comes through the touch input, a click here would step twice.
    const std::string label = layers.shown ? *layers.shown : std::string(kIdleLabel);
    ImGui::Button(label.c_str(), size);
  }
  ImGui::End();
  ImGui::PopStyleVar();
  return Eigen::AlignedBox2f(Eigen::Vector2f(position.x, position.y),
                             Eigen::Vector2f(position.x + size.x, position.y + size.y));
}

}  // namespace z13::raylib::gui
