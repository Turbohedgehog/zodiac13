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

#include "stats_overlay.h"

#include <algorithm>

#include <Eigen/Dense>

#include <z13/components/station.h>

#include "gui_widgets.h"
#include "render/render_stats.h"

namespace z13::raylib::gui {

namespace {

// Long enough for steady figures, short enough to follow a change.
constexpr std::chrono::milliseconds kAveragingWindow {500};

float Ms(StatsOverlay::Clock::duration duration) {
  return std::chrono::duration<float, std::milli>(duration).count();
}

}  // namespace

void StatsOverlay::CountFrame(Clock::time_point now) {
  if (last_frame_) {
    window_worst_ms_ = std::max(window_worst_ms_, Ms(now - *last_frame_));
  }
  last_frame_ = now;
  if (!window_start_) {
    window_start_ = now;
    return;
  }
  ++window_frames_;
  const Clock::duration elapsed = now - *window_start_;
  if (elapsed < kAveragingWindow) {
    return;
  }
  average_ms_ = Ms(elapsed) / static_cast<float>(window_frames_);
  fps_ = 1000.f / average_ms_;
  worst_ms_ = window_worst_ms_;
  window_start_ = now;
  window_frames_ = 0;
  window_worst_ms_ = 0.f;
}

float StatsOverlay::Draw(const RenderStats* render, float top) const {
  PlaceOverlay(top);
  float below = top;
  if (ImGui::Begin("##stats", nullptr, kOverlayFlags)) {
    ImGui::Text("%.0f fps, %.1f ms, worst %.1f ms", fps_, average_ms_, worst_ms_);
    if (render != nullptr) {
      ImGui::Text("Chunks %zu / %zu, meshes %zu", render->chunks_drawn, render->chunks, render->meshes_drawn);
      ImGui::Text("Glass %zu / %zu", render->glass_drawn, render->glass);
      if (render->rooms_seen) {
        ImGui::Text("Rooms seen %zu, %.0f us", *render->rooms_seen, render->culling_us);
      } else {
        ImGui::TextUnformatted("Rooms don't cull");
      }
      const z13::CameraPose& camera = render->camera;
      const Eigen::Vector3i cell = (camera.eye / z13::station::kCellSize).array().floor().cast<int>();
      ImGui::Text("Eye x %.2f, y %.2f, z %.2f m", camera.eye.x(), camera.eye.y(), camera.eye.z());
      ImGui::Text("Cell x %d, y %d, z %d", cell.x(), cell.y(), cell.z());
      ImGui::Text("Yaw %.1f, pitch %.1f", camera.yaw_deg, camera.pitch_deg);
    }
    below = OverlayTopBelow();
  }
  ImGui::End();
  return below;
}

}  // namespace z13::raylib::gui
