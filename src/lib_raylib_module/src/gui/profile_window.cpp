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

#include <algorithm>
#include <format>
#include <memory>

#include <imgui.h>

#include <z13/components/station_mode.h>

#include "gui_widgets.h"
#include "profile_teleport.h"
#include "render/frame_profile.h"

namespace z13::raylib::gui {

namespace {

// The report's box in font heights; on a small screen, what the title, hint, buttons and padding leave.
constexpr float kReportWidthEm = 48.f;
constexpr float kReportHeightEm = 22.f;
constexpr float kChromeHeightEm = 10.f;
constexpr float kChromeWidthEm = 4.f;
constexpr float kMinReportHeightEm = 4.f;

ImVec2 ReportSize() {
  const ImVec2 display = ImGui::GetIO().DisplaySize;
  return {std::min(Em(kReportWidthEm), display.x - Em(kChromeWidthEm)),
          std::max(std::min(Em(kReportHeightEm), display.y - Em(kChromeHeightEm)), Em(kMinReportHeightEm))};
}

}  // namespace

ProfileWindow::ProfileWindow(flecs::world world) : Window(world, "Profile"), report_(ProfileReport(world)) {
  view_.SetText(report_);
  if (const auto* choice = world.try_get<z13::station::StationSceneChoice>(); choice != nullptr && choice->scene) {
    viewpoints_ = z13::building::primitives::StationViewpoints(*choice->scene);
  }
}

// The paused game records nothing, so a new measuring starts by leaving the menu.
void ProfileWindow::ResumeAndMeasure() {
  World().get_mut<FrameProfile>().timeline.Clear();
  RequestCloseMenu();
}

void ProfileWindow::DrawBody() {
  view_.Draw(ReportSize());
  ImGui::TextUnformatted("Drag: scroll. Hold a line: select. Tap: grow, narrow, clear.");
  ImGui::BeginDisabled(!view_.HasSelection());
  if (ImGui::Button("Copy selection")) {
    ImGui::SetClipboardText(view_.Selection().c_str());
    status_ = "Copied the selected lines.";
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button("Copy and save")) {
    status_ = CaptureProfileReport(report_);
  }
  ImGui::SameLine();
  if (ImGui::Button("Resume and measure")) {
    ResumeAndMeasure();
  }
  ImGui::SameLine();
  if (ImGui::Button("Back")) {
    RequestPop();
  }
  for (const z13::building::primitives::StationViewpoint& viewpoint : viewpoints_) {
    if (ImGui::Button(std::format("Go to {} and measure", viewpoint.label).c_str())) {
      if (const auto moved = TeleportLocalPlayer(World(), viewpoint.position_m)) {
        ResumeAndMeasure();
      } else {
        status_ = std::format("Not moved: {}", moved.error());
      }
    }
  }
  if (!status_.empty()) {
    ImGui::TextWrapped("%s", status_.c_str());
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
