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
  const double window_ms = std::chrono::duration<double, std::milli>(window_).count();
  while (frames_.size() > 1) {
    const double oldest_ms = frames_.front().period_ms;
    if (covered_ms_ - oldest_ms < window_ms) {
      break;
    }
    covered_ms_ -= oldest_ms;
    frames_.pop_front();
  }
}

std::vector<FrameSample> FrameRecorder::Frames() const {
  return {frames_.begin(), frames_.end()};
}

void FrameRecorder::Clear() {
  *this = FrameRecorder(window_);
}

}  // namespace z13
