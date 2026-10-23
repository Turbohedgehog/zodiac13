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
#include <optional>
#include <span>

#include <lib_core/utils/frame_recorder.h>
#include <lib_core/utils/system_times.h>

namespace z13 {

inline constexpr std::chrono::seconds kSystemTotalsInterval {1};

// For telling an empty report from a broken one.
struct FrameTimelineCounts {
  size_t started {};
  size_t paused {};
  // The end of a pause, or a call to Clear.
  size_t cleared {};
};

// Turns the marks a host frame leaves into the FrameSample the recorder keeps when the next frame
// starts, so the period is start to start.
class FrameTimeline {
 public:
  using Clock = std::chrono::steady_clock;

  // `stages` are the previous frame's and count only if it was drawn. A paused game's frames are
  // left out, and the frames recorded before the pause are dropped when it ends.
  void StartFrame(
      Clock::time_point now, bool paused, std::span<const FrameStage> stages, const SystemSampler& sample_systems);
  // A frame that never calls this wasn't drawn.
  void MarkDrawStart(Clock::time_point now);
  void MarkPresent(double ms);

  const FrameRecorder& Recorder() const { return recorder_; }
  const FrameTimelineCounts& Counts() const { return counts_; }
  void Clear();

 private:
  FrameRecorder recorder_;
  FrameTimelineCounts counts_;
  std::optional<Clock::time_point> start_;
  std::optional<Clock::time_point> draw_start_;
  double present_ms_ {};
  bool paused_ {};
  std::optional<Clock::time_point> systems_sampled_;
};

}  // namespace z13
