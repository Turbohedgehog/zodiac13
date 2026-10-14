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
#include <format>
#include <iterator>
#include <string>

#include <Eigen/Dense>

#include <z13/components/block.h>

#include "render/render_stats.h"

namespace z13::raylib::gui {

namespace {

// Long enough for steady figures, short enough to follow a change.
constexpr std::chrono::milliseconds kAveragingWindow {500};

float Ms(StatsOverlay::Clock::duration duration) {
  return std::chrono::duration<float, std::milli>(duration).count();
}

std::string RenderLines(const RenderStats& render) {
  std::string text;
  auto out = std::back_inserter(text);
  std::format_to(out, "Chunks {} / {}, meshes {}\n", render.chunks_drawn, render.chunks, render.meshes_drawn);
  std::format_to(out, "Glass chunks {} / {}\n", render.glass_chunks_drawn, render.glass_chunks);
  std::format_to(out, "Lamps {}, with shadows {}, {:.2f} ms\n", render.lights, render.shadowed_lights,
                 render.times.shadows_ms);
  if (render.rooms_seen) {
    std::format_to(out, "Rooms seen {}, {:.0f} us\n", *render.rooms_seen, render.culling_us);
  } else {
    std::format_to(out, "Rooms don't cull\n");
  }
  const z13::CameraPose& camera = render.camera;
  const Eigen::Vector3i cell = (camera.eye / z13::station::kCellSize).array().floor().cast<int>();
  std::format_to(out, "Eye x {:.2f}, y {:.2f}, z {:.2f} m\n", camera.eye.x(), camera.eye.y(), camera.eye.z());
  std::format_to(out, "Cell x {}, y {}, z {}\n", cell.x(), cell.y(), cell.z());
  std::format_to(out, "Yaw {:.1f}, pitch {:.1f}", camera.look.yaw_deg, camera.look.pitch_deg);
  return text;
}

// A read-only field the size of its text: drag to select, Ctrl+C to copy.
void DrawSelectableText(std::string& text) {
  const ImGuiStyle& style = ImGui::GetStyle();
  const ImVec2 text_size = ImGui::CalcTextSize(text.c_str());
  const ImVec2 size {text_size.x + style.FramePadding.x * 2.f + style.ScrollbarSize,
                     text_size.y + style.FramePadding.y * 2.f};
  ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4 {});
  ImGui::InputTextMultiline("##text", text.data(), text.size() + 1, size, ImGuiInputTextFlags_ReadOnly);
  ImGui::PopStyleColor();
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

float StatsOverlay::Draw(const RenderStats* render, float top, OverlayInput input) const {
  std::string text = std::format("{:.0f} fps, {:.1f} ms, worst {:.1f} ms", fps_, average_ms_, worst_ms_);
  if (render != nullptr) {
    text += "\n" + RenderLines(*render);
  }
  PlaceOverlay(top);
  float below = top;
  if (ImGui::Begin("##stats", nullptr, OverlayFlags(input))) {
    if (input == OverlayInput::kSelectable) {
      DrawSelectableText(text);
    } else {
      ImGui::TextUnformatted(text.c_str());
    }
    below = OverlayTopBelow();
  }
  ImGui::End();
  return below;
}

}  // namespace z13::raylib::gui
