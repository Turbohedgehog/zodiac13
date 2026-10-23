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

#include "profile_window.h"

#include <format>
#include <memory>

#include <imgui.h>

#include "gui_widgets.h"
#include "render/frame_profile.h"

namespace z13::raylib::gui {

namespace {

// The report's box, in font heights: wide enough for its longest line, tall enough for the
// tables, scrolling past that.
constexpr float kReportWidthEm = 48.f;
constexpr float kReportHeightEm = 22.f;

}  // namespace

ProfileWindow::ProfileWindow(flecs::world world) : Window(world, "Profile"), report_(ProfileReport(world)) {
  view_.SetText(report_);
}

void ProfileWindow::DrawBody() {
  view_.Draw({Em(kReportWidthEm), Em(kReportHeightEm)});
  ImGui::TextUnformatted("Drag to scroll; press and hold a line to select.");
  ImGui::BeginDisabled(!view_.HasSelection());
  if (ImGui::Button("Copy selection", ButtonSize())) {
    ImGui::SetClipboardText(view_.Selection().c_str());
    status_ = "Copied the selected lines.";
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button("Copy and save", ButtonSize())) {
    status_ = CaptureProfileReport(report_);
  }
  // The paused game records nothing, so a new measuring starts by leaving the menu.
  if (ImGui::Button("Resume and measure", ButtonSize())) {
    World().get_mut<FrameProfile>().timeline.Clear();
    RequestCloseMenu();
  }
  if (!status_.empty()) {
    ImGui::TextWrapped("%s", status_.c_str());
  }
  ImGui::Separator();
  if (ImGui::Button("Back", ButtonSize())) {
    RequestPop();
  }
}

WindowPtr MakeProfileWindow(flecs::world world) {
  return std::make_shared<ProfileWindow>(world);
}

std::string CaptureProfileReport(const std::string& report) {
  ImGui::SetClipboardText(report.c_str());
  const auto path = SaveProfileReport(report);
  return path ? std::format("Copied; saved to {}", path->string()) : std::format("Copied; not saved: {}", path.error());
}

}  // namespace z13::raylib::gui
