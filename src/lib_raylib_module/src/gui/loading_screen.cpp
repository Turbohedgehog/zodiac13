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

#include "loading_screen.h"

#include <imgui.h>

namespace z13::raylib::gui {

namespace {

constexpr ImU32 kBackgroundColor = IM_COL32(0, 0, 0, 255);
constexpr ImU32 kTextColor = IM_COL32(255, 255, 255, 255);
constexpr float kHalf = 0.5f;

}  // namespace

void DrawLoadingScreen(const std::string& text) {
  const ImVec2 display = ImGui::GetIO().DisplaySize;
  const ImVec2 size = ImGui::CalcTextSize(text.c_str());
  ImDrawList& draw_list = *ImGui::GetForegroundDrawList();
  draw_list.AddRectFilled({0.f, 0.f}, display, kBackgroundColor);
  draw_list.AddText({(display.x - size.x) * kHalf, (display.y - size.y) * kHalf}, kTextColor, text.c_str());
}

}  // namespace z13::raylib::gui
