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
#include <utility>
#include <vector>

#include <imgui.h>

#include <lib_core/utils/text_lines.h>

namespace z13::raylib::gui {

// A drag scrolls (ImGui reads a finger as a mouse, so a text field would select); a long press
// selects lines, later taps grow, narrow and clear the selection (z13::SelectionAfterTap).
// A long line wraps at the box's width, so nothing needs scrolling sideways.
class ReportView {
 public:
  // `id` tells two views of one window apart.
  explicit ReportView(std::string id = "##report") : id_(std::move(id)) {}

  void SetText(const std::string& text);
  // Lines drawn each in its own colour, which `colors` gives by line (none: the text colour).
  void SetLines(std::vector<std::string> lines, std::vector<ImU32> colors = {});
  void Draw(const ImVec2& size);

  bool HasSelection() const { return selection_.has_value(); }
  std::string Selection() const;
  std::string AllText() const;
  // The box was scrolled to its last line when last drawn: a log that follows its end stays there.
  bool AtEnd() const { return at_end_; }
  void ScrollToEnd() { scroll_to_end_ = true; }

 private:
  enum class Gesture { kNone, kPressed, kScrolling, kSelecting };

  // Where each line starts, from the top of the text, with the wrapped height of every line; remade when the lines or
  // the width change.
  void Layout(float wrap_width, float line_height);
  void HandleGesture(const ImVec2& origin);
  size_t LineAt(float y, const ImVec2& origin) const;
  float TextHeight() const { return tops_.empty() ? 0.f : tops_.back(); }
  void ScrollNearTheEdge(float y);

  std::string id_;
  std::vector<std::string> lines_;
  std::vector<ImU32> colors_;
  // lines_.size() + 1 offsets: the start of each line and the end of the last.
  std::vector<float> tops_;
  std::optional<float> laid_width_;
  bool at_end_ = true;
  bool scroll_to_end_ {};
  std::optional<z13::LineSelection> selection_;
  Gesture gesture_ {};
  size_t anchor_ {};
};

}  // namespace z13::raylib::gui
