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
#include <span>

#include <lib_core/utils/frame_recorder.h>
#include <lib_core/utils/system_times.h>

namespace z13 {

// How often the systems' time is sampled for a frame report.
inline constexpr std::chrono::seconds kSystemTotalsInterval {1};

// Turns the marks a host frame leaves (where it started, where its drawing started, how long the
// swap took) into the FrameSample the recorder keeps. A frame is recorded when the next one
// starts, so its period is the time between the two starts.
class FrameTimeline {
 public:
  using Clock = std::chrono::steady_clock;

  // The frame about to run starts at `now`. `stages` are the previous frame's drawing stages and
  // count only if it was drawn (MarkDrawStart); `sample_systems`, if set, is called once an interval
  // for the systems' totals. A paused game's frames aren't gameplay: they are left out, and the
  // frames recorded before the pause are dropped when it ends.
  void StartFrame(
      Clock::time_point now, bool paused, std::span<const FrameStage> stages, const SystemSampler& sample_systems);
  // The frame running now starts drawing (a frame that never calls this wasn't drawn).
  void MarkDrawStart(Clock::time_point now);
  void MarkPresent(double ms);

  const FrameRecorder& Recorder() const { return recorder_; }
  void Clear();

 private:
  FrameRecorder recorder_;
  std::optional<Clock::time_point> start_;
  std::optional<Clock::time_point> draw_start_;
  double present_ms_ {};
  bool paused_ {};
  std::optional<Clock::time_point> systems_sampled_;
};

}  // namespace z13
