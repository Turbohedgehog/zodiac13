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
#include <cstddef>
#include <deque>
#include <string_view>
#include <vector>

#include <lib_core/utils/system_times.h>

namespace z13 {

// A timed part of a frame's drawing. `name` is a constant its recorder owns.
struct FrameStage {
  std::string_view name;
  double ms {};
};

// One host frame: what the player waited for and where the time went. Whatever the parts don't
// add up to (the GUI, waiting for the tick pacer) is the rest of period_ms.
struct FrameSample {
  // From the end of the previous frame to the end of this one.
  double period_ms {};
  // The world's ticks before the frame was drawn.
  double simulation_ms {};
  // The swap, where a wait for vsync or the GPU lands.
  double present_ms {};
  // The drawing, in the order it ran; empty for a frame that wasn't drawn.
  std::vector<FrameStage> stages;
};

inline constexpr std::chrono::seconds kFrameReportWindow {10};

// The last few seconds of frames, and the systems' time over the same stretch.
class FrameRecorder {
 public:
  explicit FrameRecorder(std::chrono::milliseconds window = kFrameReportWindow);

  // Drops the frames from the front while the rest still cover the window.
  void Add(FrameSample sample);
  // `totals` is every system's time so far (SystemTimes), taken now: the frames added since an
  // earlier call give the mean per frame. Kept for as long as the frames they bracket.
  void AddSystemTotals(std::vector<SystemTime> totals);

  std::vector<FrameSample> Frames() const;
  // Each system's mean per frame between the oldest and the newest totals still held; nothing
  // before there are two.
  std::vector<SystemTime> SystemsPerFrame() const;

  size_t Size() const { return frames_.size(); }
  void Clear();

 private:
  // The system totals after `frame` frames had been added.
  struct SystemTotals {
    size_t frame {};
    std::vector<SystemTime> totals;
  };

  std::chrono::milliseconds window_ {};
  std::deque<FrameSample> frames_;
  double covered_ms_ {};
  // Frames added over the recorder's life, and so the index of frames_.front().
  size_t added_ {};
  std::deque<SystemTotals> systems_;
};

}  // namespace z13
