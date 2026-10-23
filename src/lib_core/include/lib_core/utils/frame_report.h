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

#include <cstddef>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <lib_core/utils/frame_recorder.h>
#include <lib_core/utils/system_times.h>

namespace z13 {

struct FrameSummary {
  size_t frames {};
  double covered_ms {};
  double mean_fps {};
  double median_fps {};
  // The mean fps of the slowest 1% of frames.
  double low_fps {};
  double mean_ms {};
  double median_ms {};
  double p99_ms {};
  double worst_ms {};
};

FrameSummary SummarizeFrames(std::span<const FrameSample> frames);

struct FrameShare {
  std::string name;
  double ms {};
  double share {};
};

// Largest first; the rest is the period the parts don't account for.
std::vector<FrameShare> TimeShares(std::span<const FrameSample> frames);

// Counters are formatted text ("98 / 117"); `system_paths` name the entries of the frames' system_ms.
struct FrameReportInput {
  std::vector<std::pair<std::string, std::string>> header;
  std::vector<std::pair<std::string, std::string>> counters;
  std::vector<std::string> system_paths;
};

// Each system's mean ms per frame over the frames that know theirs, heaviest first.
std::vector<SystemTime> SystemMeans(std::span<const FrameSample> frames, std::span<const std::string> paths);

// A text a player can copy and send.
std::string FrameReport(std::span<const FrameSample> frames, const FrameReportInput& input);

}  // namespace z13
