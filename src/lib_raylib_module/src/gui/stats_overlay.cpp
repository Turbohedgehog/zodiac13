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

#include "render/render_stats.h"

namespace z13::raylib::gui {

namespace {

// Long enough for steady figures, short enough to follow a change.
constexpr std::chrono::milliseconds kAveragingWindow {500};
constexpr float kMargin = 10.f;
constexpr float kBackgroundAlpha = 0.5f;
constexpr ImGuiWindowFlags kOverlayFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                           ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoFocusOnAppearing |
                                           ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoSavedSettings;

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

void StatsOverlay::Draw(const RenderStats* render) const {
  ImGui::SetNextWindowPos({kMargin, kMargin});
  ImGui::SetNextWindowBgAlpha(kBackgroundAlpha);
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
    }
  }
  ImGui::End();
}

}  // namespace z13::raylib::gui
