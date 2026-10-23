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

namespace z13::raylib::gui {

// The lines a selection covers, both included; `first` is where it started.
struct LineSelection {
  size_t first {};
  size_t last {};
};

// A report's text in a box a drag scrolls (ImGui reads a finger as a mouse, so a text field would
// select instead) and a long press on a line selects: the finger then moves the selection over
// the lines, and a tap clears it.
class ReportView {
 public:
  void SetText(const std::string& text);
  void Draw(const ImVec2& size);

  bool HasSelection() const { return selection_.has_value(); }
  // The selected lines, each ended by a line feed.
  std::string Selection() const;

 private:
  enum class Gesture { kNone, kPressed, kScrolling, kSelecting };

  void HandleGesture(const ImVec2& origin, float line_height);
  size_t LineAt(float y, const ImVec2& origin, float line_height) const;
  void ScrollNearTheEdge(float y);

  std::vector<std::string> lines_;
  std::optional<LineSelection> selection_;
  Gesture gesture_ {};
  size_t anchor_ {};
};

}  // namespace z13::raylib::gui
