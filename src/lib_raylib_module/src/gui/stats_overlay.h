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

#include <chrono>
#include <optional>

#include <imgui.h>

namespace z13::raylib {
struct RenderStats;
}  // namespace z13::raylib

namespace z13::raylib::gui {

// A debug key, not a game action: it never goes through the command stream.
inline constexpr ImGuiKey kStatsOverlayKey = ImGuiKey_F3;

// Frame rate and what the renderer drew, in a corner of the screen; F3 shows and hides it.
// Singleton; never state.
class StatsOverlay {
 public:
  using Singleton = void;
  using Clock = std::chrono::steady_clock;

  void Toggle() { visible_ = !visible_; }
  bool Visible() const { return visible_; }

  // Called once per drawn frame, shown or not, so the first figures shown are current.
  void CountFrame(Clock::time_point now);

  void Draw(const RenderStats* render) const;

 private:
  bool visible_ {};
  std::optional<Clock::time_point> last_frame_;
  std::optional<Clock::time_point> window_start_;
  int window_frames_ {};
  float window_worst_ms_ {};
  // As of the last full window.
  float fps_ {};
  float average_ms_ {};
  float worst_ms_ {};
};

}  // namespace z13::raylib::gui
