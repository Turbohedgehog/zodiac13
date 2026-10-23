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

#include <lib_core/utils/text_lines.h>

#include "gui_widgets.h"

namespace z13::raylib::gui {

namespace {

constexpr const char* kBoxId = "##report";
constexpr float kLongPressSeconds = 0.45f;
// A finger near the box's top or bottom edge scrolls it, so a selection can reach lines out of view.
constexpr float kEdgeEm = 2.f;
constexpr float kEdgeScrollEmPerSecond = 12.f;
constexpr ImU32 kSelectionColor = IM_COL32(70, 110, 200, 140);

}  // namespace

void ReportView::SetText(const std::string& text) {
  lines_ = z13::SplitLines(text);
  selection_.reset();
  gesture_ = Gesture::kNone;
}

std::string ReportView::Selection() const {
  return selection_ ? z13::JoinLines(lines_, selection_->first, selection_->last) : std::string();
}

size_t ReportView::LineAt(float y, const ImVec2& origin, float line_height) const {
  const float line = std::clamp((y - origin.y) / line_height, 0.f, static_cast<float>(lines_.size() - 1));
  return static_cast<size_t>(line);
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

void ReportView::HandleGesture(const ImVec2& origin, float line_height) {
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
        selection_.reset();
        gesture_ = Gesture::kNone;
      } else if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        gesture_ = Gesture::kScrolling;
      } else if (io.MouseDownDuration[ImGuiMouseButton_Left] >= kLongPressSeconds) {
        anchor_ = LineAt(io.MousePos.y, origin, line_height);
        selection_ = LineSelection {.first = anchor_, .last = anchor_};
        gesture_ = Gesture::kSelecting;
      }
      break;
    case Gesture::kScrolling:
      ImGui::SetScrollX(ImGui::GetScrollX() - io.MouseDelta.x);
      ImGui::SetScrollY(ImGui::GetScrollY() - io.MouseDelta.y);
      gesture_ = down ? Gesture::kScrolling : Gesture::kNone;
      break;
    case Gesture::kSelecting:
      selection_ = LineSelection {.first = anchor_, .last = LineAt(io.MousePos.y, origin, line_height)};
      ScrollNearTheEdge(io.MousePos.y);
      gesture_ = down ? Gesture::kSelecting : Gesture::kNone;
      break;
    case Gesture::kNone:
      break;
  }
}

void ReportView::Draw(const ImVec2& size) {
  if (ImGui::BeginChild(kBoxId, size, ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar)) {
    // Lines touch, so a line's place is its index times the text height.
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {ImGui::GetStyle().ItemSpacing.x, 0.f});
    const float line_height = ImGui::GetTextLineHeight();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    HandleGesture(origin, line_height);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float left = ImGui::GetWindowPos().x;
    const float right = left + ImGui::GetWindowWidth();
    const size_t low = selection_ ? std::min(selection_->first, selection_->last) : 0;
    const size_t high = selection_ ? std::max(selection_->first, selection_->last) : 0;
    for (size_t i = 0; i < lines_.size(); ++i) {
      if (selection_ && i >= low && i <= high) {
        const float top = origin.y + static_cast<float>(i) * line_height;
        draw->AddRectFilled({left, top}, {right, top + line_height}, kSelectionColor);
      }
      ImGui::TextUnformatted(lines_[i].c_str(), lines_[i].c_str() + lines_[i].size());
    }
    ImGui::PopStyleVar();
  }
  ImGui::EndChild();
}

}  // namespace z13::raylib::gui
