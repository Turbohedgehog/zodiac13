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


#include <lib_core/utils/system_stats.h>

#include <algorithm>
#include <format>
#include <functional>
#include <numeric>
#include <ranges>

namespace z13 {

namespace {

double MedianOf(std::vector<double> values) {
  std::ranges::sort(values);
  const size_t middle = values.size() / 2;
  return values.size() % 2 == 1 ? values[middle] : (values[middle - 1] + values[middle]) / 2.0;
}

}  // namespace

void SystemFrameStats::AddFrame(const std::vector<SystemTime>& before, const std::vector<SystemTime>& after) {
  std::unordered_map<std::string, double> earlier;
  for (const SystemTime& system : before) {
    earlier.emplace(system.path, system.ms);
  }
  for (const SystemTime& system : after) {
    const auto found = earlier.find(system.path);
    std::vector<double>& samples = samples_[system.path];
    samples.resize(static_cast<size_t>(frames_), 0.0);  // a system first seen late sat out the frames before
    samples.push_back(system.ms - (found != earlier.end() ? found->second : 0.0));
  }
  ++frames_;
}

std::vector<SystemSummary> SystemFrameStats::Summary() const {
  std::vector<SystemSummary> summary;
  for (const auto& [path, recorded] : samples_) {
    std::vector<double> samples = recorded;
    samples.resize(static_cast<size_t>(frames_), 0.0);
    const auto [min, max] = std::ranges::minmax(samples);
    summary.push_back({.path = path,
                       .mean_ms = std::accumulate(samples.begin(), samples.end(), 0.0) / static_cast<double>(frames_),
                       .min_ms = min,
                       .max_ms = max,
                       .median_ms = MedianOf(samples)});
  }
  std::ranges::sort(summary, std::greater<>(), &SystemSummary::mean_ms);
  return summary;
}

std::string SystemSummaryRows(std::string_view prefix, const std::vector<SystemSummary>& summary, double min_mean_ms) {
  std::string rows;
  for (const SystemSummary& system : summary) {
    if (system.mean_ms >= min_mean_ms) {
      rows += std::format("{}\"{}\",{:.4f},{:.4f},{:.4f},{:.4f}\n", prefix, system.path, system.mean_ms, system.min_ms,
                          system.median_ms, system.max_ms);
    }
  }
  return rows;
}

}  // namespace z13
