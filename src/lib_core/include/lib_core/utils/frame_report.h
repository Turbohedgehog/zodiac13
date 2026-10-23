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

// How a run of frames went, in milliseconds a frame (fps where named).
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

// A part of a frame's time and its share of the mean period.
struct FrameShare {
  std::string name;
  double ms {};
  double share {};
};

// Where the mean frame's time goes, largest first: the simulation, each drawing stage by name, the
// swap, and the rest of the period the parts don't account for.
std::vector<FrameShare> TimeShares(std::span<const FrameSample> frames);

// What a report adds to the frames: lines about the device and the run, counters the frames don't
// carry (as text, "98 / 117"), and each system's mean time per frame.
struct FrameReportInput {
  std::vector<std::pair<std::string, std::string>> header;
  std::vector<std::pair<std::string, std::string>> counters;
  std::vector<SystemTime> systems;
};

// A text a player can copy and send: the summary, where the time goes, the heaviest systems, the
// counters and the slowest frames each with the part that cost most.
std::string FrameReport(std::span<const FrameSample> frames, const FrameReportInput& input);

}  // namespace z13
