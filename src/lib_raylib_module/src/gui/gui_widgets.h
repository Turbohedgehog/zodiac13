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

inline constexpr ImVec2 kButtonSize{200.f, 0.f};
inline constexpr ImVec4 kErrorColor{1.f, 0.4f, 0.4f, 1.f};

inline void DrawError(std::string_view text) {
  ImGui::TextColored(kErrorColor, "%s", std::string(text).c_str());
}

}  // namespace z13::raylib::gui
