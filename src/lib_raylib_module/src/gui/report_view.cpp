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

constexpr std::string_view kBoxId = "##report";
constexpr float kLongPressSeconds = 0.45f;
// A finger near the top or bottom edge scrolls the box, so a selection can reach lines out of view.
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
  const auto last = static_cast<float>(std::max<size_t>(lines_.size(), 1) - 1);
  const float line = std::clamp((y - origin.y) / line_height, 0.f, last);
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
        const bool on_text = io.MousePos.y < origin.y + static_cast<float>(lines_.size()) * line_height;
        selection_ = on_text ? z13::SelectionAfterTap(selection_, LineAt(io.MousePos.y, origin, line_height))
                             : std::nullopt;
        gesture_ = Gesture::kNone;
      } else if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        gesture_ = Gesture::kScrolling;
      } else if (io.MouseDownDuration[ImGuiMouseButton_Left] >= kLongPressSeconds) {
        anchor_ = LineAt(io.MousePos.y, origin, line_height);
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
      selection_ = z13::LineSelection {.first = anchor_, .last = LineAt(io.MousePos.y, origin, line_height)};
      ScrollNearTheEdge(io.MousePos.y);
      gesture_ = down ? Gesture::kSelecting : Gesture::kNone;
      break;
    case Gesture::kNone:
      break;
  }
}

void ReportView::Draw(const ImVec2& size) {
  if (ImGui::BeginChild(kBoxId.data(), size, ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar)) {
    // Lines touch, so a line's place is its index times the text height.
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {ImGui::GetStyle().ItemSpacing.x, 0.f});
    const float line_height = ImGui::GetTextLineHeight();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    HandleGesture(origin, line_height);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float left = ImGui::GetWindowPos().x;
    const float right = left + ImGui::GetWindowWidth();
    for (size_t i = 0; i < lines_.size(); ++i) {
      if (selection_ && i >= selection_->Low() && i <= selection_->High()) {
        const float top = origin.y + static_cast<float>(i) * line_height;
        draw->AddRectFilled({left, top}, {right, top + line_height}, kSelectionColor);
      }
      const std::string& line = lines_[i];
      ImGui::TextUnformatted(line.c_str(), line.c_str() + line.size());
    }
    ImGui::PopStyleVar();
  }
  ImGui::EndChild();
}

}  // namespace z13::raylib::gui
