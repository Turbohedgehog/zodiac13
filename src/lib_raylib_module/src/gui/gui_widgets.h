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

#include <string>
#include <string_view>

#include <imgui.h>

namespace z13::raylib::gui {

// Widths in font heights, so they follow the display scale with the font (GuiSystem).
inline constexpr float kButtonWidthEm = 15.f;
inline constexpr float kAddressFieldWidthEm = 15.f;
inline constexpr ImVec4 kErrorColor{1.f, 0.4f, 0.4f, 1.f};

inline float Em(float em) {
  return em * ImGui::GetFontSize();
}

inline ImVec2 ButtonSize() {
  return {Em(kButtonWidthEm), 0.f};
}

inline float AddressFieldWidth() {
  return Em(kAddressFieldWidthEm);
}

inline void DrawError(std::string_view text) {
  ImGui::TextColored(kErrorColor, "%s", std::string(text).c_str());
}

// Debug overlays stack down the top-left corner, this far apart and from the screen's edges.
inline constexpr float kOverlayMarginEm = 0.75f;
inline constexpr float kOverlayBackgroundAlpha = 0.5f;
inline constexpr ImGuiWindowFlags kOverlayFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                                  ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoFocusOnAppearing |
                                                  ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoSavedSettings;

inline float FirstOverlayTop() {
  return Em(kOverlayMarginEm);
}

inline void PlaceOverlay(float top) {
  ImGui::SetNextWindowPos({Em(kOverlayMarginEm), top});
  ImGui::SetNextWindowBgAlpha(kOverlayBackgroundAlpha);
}

// Where the overlay below the current window starts; call between Begin and End.
inline float OverlayTopBelow() {
  return ImGui::GetWindowPos().y + ImGui::GetWindowSize().y + Em(kOverlayMarginEm);
}

}  // namespace z13::raylib::gui
