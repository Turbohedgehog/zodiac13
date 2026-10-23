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

#include <lib_core/utils/frame_recorder.h>

#include <utility>

namespace z13 {

FrameRecorder::FrameRecorder(std::chrono::milliseconds window) : window_(window) {
}

void FrameRecorder::Add(FrameSample sample) {
  covered_ms_ += sample.period_ms;
  frames_.push_back(std::move(sample));
  ++added_;
  const double window_ms = std::chrono::duration<double, std::milli>(window_).count();
  while (frames_.size() > 1) {
    const double oldest_ms = frames_.front().period_ms;
    if (covered_ms_ - oldest_ms < window_ms) {
      break;
    }
    covered_ms_ -= oldest_ms;
    frames_.pop_front();
  }
  const size_t first = added_ - frames_.size();
  while (systems_.size() > 1 && systems_[1].frame <= first) {
    systems_.pop_front();
  }
}

void FrameRecorder::AddSystemTotals(std::vector<SystemTime> totals) {
  systems_.push_back({.frame = added_, .totals = std::move(totals)});
}

std::vector<FrameSample> FrameRecorder::Frames() const {
  return {frames_.begin(), frames_.end()};
}

std::vector<SystemTime> FrameRecorder::SystemsPerFrame() const {
  if (systems_.size() < 2) {
    return {};
  }
  const SystemTotals& oldest = systems_.front();
  const SystemTotals& newest = systems_.back();
  if (newest.frame == oldest.frame) {
    return {};
  }
  return SystemTimesPerFrame(oldest.totals, newest.totals, static_cast<int>(newest.frame - oldest.frame));
}

void FrameRecorder::Clear() {
  *this = FrameRecorder(window_);
}

}  // namespace z13
