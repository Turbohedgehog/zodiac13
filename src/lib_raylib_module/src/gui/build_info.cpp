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

#include "build_info.h"

#include <format>
#include <string>

#include <imgui.h>

#include <lib_core/version.h>

#include "gui_widgets.h"

namespace z13::raylib::gui {

namespace {

// From the screen's edges, in font sizes.
constexpr float kMarginEm = 0.5f;
constexpr ImU32 kTextColor = IM_COL32(255, 255, 255, 140);

}  // namespace

void DrawBuildInfo() {
  const std::string text = std::format("v{}  {}", z13::kEngineVersion, z13::kBuildDate);
  const ImVec2 size = ImGui::CalcTextSize(text.c_str());
  const ImVec2 display = ImGui::GetIO().DisplaySize;
  const float margin = Em(kMarginEm);
  ImGui::GetForegroundDrawList()->AddText({display.x - size.x - margin, display.y - size.y - margin}, kTextColor,
                                          text.c_str());
}

}  // namespace z13::raylib::gui
