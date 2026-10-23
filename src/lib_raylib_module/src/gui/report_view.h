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

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include <imgui.h>

#include <lib_core/utils/text_lines.h>

namespace z13::raylib::gui {

// A drag scrolls (ImGui reads a finger as a mouse, so a text field would select); a long press
// selects lines, later taps grow, narrow and clear the selection (z13::SelectionAfterTap).
class ReportView {
 public:
  void SetText(const std::string& text);
  void Draw(const ImVec2& size);

  bool HasSelection() const { return selection_.has_value(); }
  std::string Selection() const;

 private:
  enum class Gesture { kNone, kPressed, kScrolling, kSelecting };

  void HandleGesture(const ImVec2& origin, float line_height);
  size_t LineAt(float y, const ImVec2& origin, float line_height) const;
  void ScrollNearTheEdge(float y);

  std::vector<std::string> lines_;
  std::optional<z13::LineSelection> selection_;
  Gesture gesture_ {};
  size_t anchor_ {};
};

}  // namespace z13::raylib::gui
