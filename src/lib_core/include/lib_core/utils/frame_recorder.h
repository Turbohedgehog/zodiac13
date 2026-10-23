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

namespace z13 {

// `name` is a constant its recorder owns.
struct FrameStage {
  std::string_view name;
  double ms {};
};

// One host frame; what the parts don't add up to (the GUI, the tick pacer's wait) is the rest of period_ms.
struct FrameSample {
  double period_ms {};
  double simulation_ms {};
  // The swap, where a wait for vsync or the GPU lands.
  double present_ms {};
  // Empty for a frame that wasn't drawn.
  std::vector<FrameStage> stages;
  // What each system spent, in ms, in the order of the report's system paths; empty when unknown.
  std::vector<float> system_ms;
};

inline constexpr std::chrono::seconds kFrameReportWindow {10};

class FrameRecorder {
 public:
  explicit FrameRecorder(std::chrono::milliseconds window = kFrameReportWindow);

  void Add(FrameSample sample);
  std::vector<FrameSample> Frames() const;
  size_t Size() const { return frames_.size(); }
  void Clear();

 private:
  std::chrono::milliseconds window_ {};
  std::deque<FrameSample> frames_;
  double covered_ms_ {};
};

}  // namespace z13
