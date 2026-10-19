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

#include "touch_hud.h"

#include <algorithm>
#include <string>

#include <Eigen/Core>
#include <imgui.h>

#include <touch/touch_mapping.h>

namespace z13::raylib::gui {

namespace {

using z13::input::TouchControl;
using z13::input::TouchFinger;

constexpr ImU32 kOutlineColor = IM_COL32(255, 255, 255, 140);
constexpr ImU32 kPressedColor = IM_COL32(255, 255, 255, 90);
constexpr ImU32 kKnobColor = IM_COL32(255, 255, 255, 160);
constexpr ImU32 kLabelColor = IM_COL32(255, 255, 255, 220);
constexpr float kOutlineThickness = 2.f;
// The knob's radius as a part of the stick's.
constexpr float kKnobScale = 0.4f;

// TouchControls is in window pixels; ImGui draws in its own units.
ImVec2 ToImGui(const Eigen::Vector2f& pixels, const Eigen::Vector2f& screen_size) {
  const ImVec2 display = ImGui::GetIO().DisplaySize;
  return {pixels.x() / screen_size.x() * display.x, pixels.y() / screen_size.y() * display.y};
}

float ToImGui(float pixels, const Eigen::Vector2f& screen_size) {
  return pixels / screen_size.y() * ImGui::GetIO().DisplaySize.y;
}

void DrawButtons(ImDrawList& draw, const z13::input::TouchControls& controls, const z13::TouchLayout& layout,
                 std::optional<std::string_view> tool_action) {
  const float radius = ToImGui(z13::input::touch::ButtonRadius(layout, controls.screen_size), controls.screen_size);
  for (const auto& button : z13::input::touch::kTouchButtons) {
    if (!z13::input::touch::InLayer(controls.layer, button.control)) {
      continue;
    }
    const Eigen::Vector2f pixels =
        z13::input::touch::ButtonCenter(layout, controls.layer, button.control, controls.screen_size);
    const ImVec2 center = ToImGui(pixels, controls.screen_size);
    if (std::ranges::contains(controls.fingers, button.control, &TouchFinger::control)) {
      draw.AddCircleFilled(center, radius, kPressedColor);
    }
    draw.AddCircle(center, radius, kOutlineColor, 0, kOutlineThickness);
    const std::string label(
        button.control == TouchControl::kPrimary ? tool_action.value_or(button.label) : button.label);
    const ImVec2 size = ImGui::CalcTextSize(label.c_str());
    draw.AddText({center.x - size.x / 2.f, center.y - size.y / 2.f}, kLabelColor, label.c_str());
  }
}

void DrawStick(ImDrawList& draw, const TouchFinger& finger, const z13::input::TouchControls& controls,
               const z13::TouchLayout& layout) {
  const float radius = z13::input::touch::StickRadius(layout, controls.screen_size);
  const Eigen::Vector2f offset = finger.position - finger.origin;
  const Eigen::Vector2f knob = finger.origin + offset * std::min(1.f, radius / std::max(offset.norm(), radius));
  draw.AddCircle(ToImGui(finger.origin, controls.screen_size), ToImGui(radius, controls.screen_size), kOutlineColor,
                 0, kOutlineThickness);
  draw.AddCircleFilled(ToImGui(knob, controls.screen_size), ToImGui(radius * kKnobScale, controls.screen_size),
                       kKnobColor);
}

}  // namespace

void DrawTouchHud(const z13::input::TouchControls& controls, const z13::TouchLayout& layout,
                  std::optional<std::string_view> tool_action) {
  if (!controls.used) {
    return;
  }
  ImDrawList& draw = *ImGui::GetBackgroundDrawList();
  DrawButtons(draw, controls, layout, tool_action);
  const auto stick = std::ranges::find(controls.fingers, TouchControl::kStick, &TouchFinger::control);
  if (stick != controls.fingers.end()) {
    DrawStick(draw, *stick, controls, layout);
  }
}

}  // namespace z13::raylib::gui
