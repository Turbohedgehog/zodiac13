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

#include "room_label.h"

#include <imgui.h>

namespace z13::raylib::gui {

namespace {

constexpr ImVec2 kPosition {10.f, 80.f};
constexpr float kBackgroundAlpha = 0.5f;
constexpr ImGuiWindowFlags kLabelFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                         ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoFocusOnAppearing |
                                         ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoSavedSettings;

}  // namespace

void DrawRoomLabel(const std::string& label) {
  ImGui::SetNextWindowPos(kPosition);
  ImGui::SetNextWindowBgAlpha(kBackgroundAlpha);
  if (ImGui::Begin("##room_label", nullptr, kLabelFlags)) {
    ImGui::TextUnformatted(label.empty() ? "No room" : label.c_str());
  }
  ImGui::End();
}

}  // namespace z13::raylib::gui
