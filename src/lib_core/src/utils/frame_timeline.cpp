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

#include <lib_core/utils/frame_timeline.h>

#include <algorithm>
#include <iterator>
#include <vector>

namespace z13 {

namespace {

double MsBetween(FrameTimeline::Clock::time_point from, FrameTimeline::Clock::time_point to) {
  return std::chrono::duration<double, std::milli>(to - from).count();
}

}  // namespace

void FrameTimeline::StartFrame(Clock::time_point now, bool paused, std::span<const FrameStage> stages,
                               std::span<const double> system_totals) {
  if (paused_ && !paused) {
    Clear();
  }
  ++counts_.started;
  counts_.paused += paused ? 1 : 0;
  if (start_ && !paused_) {
    FrameSample sample {.period_ms = MsBetween(*start_, now)};
    if (draw_start_) {
      sample.simulation_ms = MsBetween(*start_, *draw_start_);
      sample.present_ms = present_ms_;
      sample.stages.assign(stages.begin(), stages.end());
    } else {
      sample.simulation_ms = sample.period_ms;
    }
    if (system_totals.size() == system_totals_.size()) {
      std::ranges::transform(system_totals, system_totals_, std::back_inserter(sample.system_ms),
                             [](double now_ms, double before_ms) { return static_cast<float>(now_ms - before_ms); });
    }
    recorder_.Add(std::move(sample));
  }
  start_ = now;
  draw_start_.reset();
  present_ms_ = 0.0;
  paused_ = paused;
  system_totals_.assign(system_totals.begin(), system_totals.end());
}

void FrameTimeline::MarkDrawStart(Clock::time_point now) {
  draw_start_ = now;
}

void FrameTimeline::MarkPresent(double ms) {
  present_ms_ = ms;
}

void FrameTimeline::Clear() {
  ++counts_.cleared;
  recorder_.Clear();
}

}  // namespace z13
