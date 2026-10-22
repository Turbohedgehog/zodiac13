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

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <lib_core/utils/system_times.h>

namespace z13 {

// One system's time per frame over a measured run, in milliseconds.
struct SystemSummary {
  std::string path;
  double mean_ms {};
  double min_ms {};
  double max_ms {};
  double median_ms {};
};

// Collects each system's time frame by frame, for the spread (min, max, median) that the mean
// of SystemTimesPerFrame hides.
class SystemFrameStats {
 public:
  // One frame: what each system spent between the samples `before` and `after`. A system that
  // didn't run counts as 0 for that frame.
  void AddFrame(const std::vector<SystemTime>& before, const std::vector<SystemTime>& after);

  int Frames() const { return frames_; }

  // Heaviest mean first.
  std::vector<SystemSummary> Summary() const;

 private:
  std::unordered_map<std::string, std::vector<double>> samples_;
  int frames_ {};
};

// Columns of the rows SystemSummaryRows writes after a caller's own leading columns.
inline constexpr std::string_view kSystemSummaryCsvColumns = "system,mean_ms,min_ms,median_ms,max_ms";

// One CSV row per system whose mean is at least `min_mean_ms`, each starting with `prefix`
// (the caller's columns, ending in a comma); the path is quoted as a template-named system's
// holds commas.
std::string SystemSummaryRows(std::string_view prefix, const std::vector<SystemSummary>& summary, double min_mean_ms);

}  // namespace z13
