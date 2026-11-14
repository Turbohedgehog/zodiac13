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

#include "report_view.h"

#include <algorithm>
#include <string_view>

#include <lib_core/utils/text_lines.h>

#include "gui_widgets.h"

namespace z13::raylib::gui {

namespace {

// How near the bottom still counts as the end.
constexpr float kEndTolerance = 1.f;
constexpr float kLongPressSeconds = 0.45f;
// A finger near the top or bottom edge scrolls the box, so a selection can reach lines out of view.
constexpr float kEdgeEm = 2.f;
constexpr float kEdgeScrollEmPerSecond = 12.f;
constexpr ImU32 kSelectionColor = IM_COL32(70, 110, 200, 140);

}  // namespace

void ReportView::SetText(const std::string& text) {
  SetLines(z13::SplitLines(text));
}

void ReportView::SetLines(std::vector<std::string> lines, std::vector<ImU32> colors) {
  lines_ = std::move(lines);
  colors_ = std::move(colors);
  laid_width_.reset();
  selection_.reset();
  gesture_ = Gesture::kNone;
}

std::string ReportView::AllText() const {
  return lines_.empty() ? std::string() : z13::JoinLines(lines_, 0, lines_.size() - 1);
}

std::string ReportView::Selection() const {
  return selection_ ? z13::JoinLines(lines_, selection_->first, selection_->last) : std::string();
}

void ReportView::Layout(float wrap_width, float line_height) {
  if (laid_width_ == wrap_width) {
    return;
  }
  tops_.assign(1, 0.f);
  for (const std::string& line : lines_) {
    const float height =
        ImGui::CalcTextSize(line.c_str(), line.c_str() + line.size(), false, wrap_width).y;
    tops_.push_back(tops_.back() + std::max(height, line_height));
  }
  laid_width_ = wrap_width;
}

size_t ReportView::LineAt(float y, const ImVec2& origin) const {
  if (lines_.empty()) {
    return 0;
  }
  const auto after = std::upper_bound(tops_.begin(), tops_.end(), y - origin.y);
  const auto index = static_cast<size_t>(std::distance(tops_.begin(), after));
  return std::clamp<size_t>(index == 0 ? 0 : index - 1, 0, lines_.size() - 1);
}

void ReportView::ScrollNearTheEdge(float y) {
  const float top = ImGui::GetWindowPos().y;
  const float bottom = top + ImGui::GetWindowHeight();
  const float step = Em(kEdgeScrollEmPerSecond) * ImGui::GetIO().DeltaTime;
  if (y < top + Em(kEdgeEm)) {
    ImGui::SetScrollY(ImGui::GetScrollY() - step);
  } else if (y > bottom - Em(kEdgeEm)) {
    ImGui::SetScrollY(ImGui::GetScrollY() + step);
  }
}

void ReportView::HandleGesture(const ImVec2& origin) {
  const ImGuiIO& io = ImGui::GetIO();
  if (gesture_ == Gesture::kNone && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsWindowHovered()) {
    gesture_ = Gesture::kPressed;
  }
  if (gesture_ == Gesture::kNone || lines_.empty()) {
    return;
  }
  const bool down = ImGui::IsMouseDown(ImGuiMouseButton_Left);
  switch (gesture_) {
    case Gesture::kPressed:
      if (!down) {
        const bool on_text = io.MousePos.y < origin.y + TextHeight();
        selection_ = on_text ? z13::SelectionAfterTap(selection_, LineAt(io.MousePos.y, origin))
                             : std::nullopt;
        gesture_ = Gesture::kNone;
      } else if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        gesture_ = Gesture::kScrolling;
      } else if (io.MouseDownDuration[ImGuiMouseButton_Left] >= kLongPressSeconds) {
        anchor_ = LineAt(io.MousePos.y, origin);
        selection_ = z13::LineSelection {.first = anchor_, .last = anchor_};
        gesture_ = Gesture::kSelecting;
      }
      break;
    case Gesture::kScrolling:
      ImGui::SetScrollX(ImGui::GetScrollX() - io.MouseDelta.x);
      ImGui::SetScrollY(ImGui::GetScrollY() - io.MouseDelta.y);
      gesture_ = down ? Gesture::kScrolling : Gesture::kNone;
      break;
    case Gesture::kSelecting:
      selection_ = z13::LineSelection {.first = anchor_, .last = LineAt(io.MousePos.y, origin)};
      ScrollNearTheEdge(io.MousePos.y);
      gesture_ = down ? Gesture::kSelecting : Gesture::kNone;
      break;
    case Gesture::kNone:
      break;
  }
}

void ReportView::Draw(const ImVec2& size) {
  if (ImGui::BeginChild(id_.c_str(), size, ImGuiChildFlags_Borders)) {
    at_end_ = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - kEndTolerance;
    // Lines touch, so a line's place is its offset in tops_.
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {ImGui::GetStyle().ItemSpacing.x, 0.f});
    const float line_height = ImGui::GetTextLineHeight();
    Layout(ImGui::GetContentRegionAvail().x, line_height);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    HandleGesture(origin);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float left = ImGui::GetWindowPos().x;
    const float right = left + ImGui::GetWindowWidth();
    for (size_t i = 0; i < lines_.size(); ++i) {
      if (selection_ && i >= selection_->Low() && i <= selection_->High()) {
        draw->AddRectFilled({left, origin.y + tops_[i]}, {right, origin.y + tops_[i + 1]}, kSelectionColor);
      }
      const std::string& line = lines_[i];
      const bool coloured = i < colors_.size();
      if (coloured) {
        ImGui::PushStyleColor(ImGuiCol_Text, colors_[i]);
      }
      ImGui::PushTextWrapPos(0.f);
      ImGui::TextUnformatted(line.c_str(), line.c_str() + line.size());
      ImGui::PopTextWrapPos();
      if (coloured) {
        ImGui::PopStyleColor();
      }
    }
    if (scroll_to_end_) {
      ImGui::SetScrollHereY(1.f);
      scroll_to_end_ = false;
    }
    ImGui::PopStyleVar();
  }
  ImGui::EndChild();
}

}  // namespace z13::raylib::gui
