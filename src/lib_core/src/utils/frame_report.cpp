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

#include <lib_core/utils/frame_report.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <iterator>
#include <functional>
#include <numeric>
#include <ranges>

#include <lib_core/utils/statistics.h>

namespace z13 {

namespace {

constexpr double kLowFraction = 0.01;
constexpr double kPeriodPercentile = 0.99;
constexpr size_t kTopSystems = 8;
constexpr size_t kSlowFrameSystems = 3;
constexpr size_t kSlowestFrames = 8;
constexpr double kSlowFactor = 1.5;
constexpr double kPercent = 100.0;
constexpr double kMinSystemMs = 0.01;
constexpr std::string_view kSimulation = "simulation";
constexpr std::string_view kPresent = "present (swap, vsync)";
constexpr std::string_view kRest = "rest (GUI, waiting)";

double Fps(double period_ms) {
  return period_ms > 0.0 ? kMsPerSecond / period_ms : 0.0;
}

double Percentile(const std::vector<double>& sorted, double fraction) {
  if (sorted.empty()) {
    return 0.0;
  }
  const auto index = static_cast<size_t>(std::ceil(fraction * static_cast<double>(sorted.size()))) - 1;
  return sorted[std::min(index, sorted.size() - 1)];
}

// The rest is what the parts leave of the period.
std::vector<FrameStage> PartsOf(const FrameSample& frame) {
  std::vector<FrameStage> parts {{.name = kSimulation, .ms = frame.simulation_ms}};
  parts.insert(parts.end(), frame.stages.begin(), frame.stages.end());
  parts.push_back({.name = kPresent, .ms = frame.present_ms});
  const double known = std::accumulate(
      parts.begin(), parts.end(), 0.0, [](double sum, const FrameStage& part) { return sum + part.ms; });
  parts.push_back({.name = kRest, .ms = std::max(frame.period_ms - known, 0.0)});
  return parts;
}

std::string HeaviestSystems(const FrameSample& frame, std::span<const std::string> paths) {
  if (frame.system_ms.size() != paths.size()) {
    return {};
  }
  std::vector<size_t> order(paths.size());
  std::iota(order.begin(), order.end(), 0U);
  std::ranges::sort(order, std::greater<>(), [&](size_t i) { return frame.system_ms[i]; });
  std::string text;
  for (const size_t index : order | std::views::take(kSlowFrameSystems)) {
    const float ms = frame.system_ms[index];
    if (ms >= kMinSystemMs) {
      text += std::format("{}{} {:.1f}", text.empty() ? "" : ", ", paths[index], ms);
    }
  }
  return text;
}

}  // namespace

FrameSummary SummarizeFrames(std::span<const FrameSample> frames) {
  FrameSummary summary {.frames = frames.size()};
  std::vector<double> periods;
  for (const FrameSample& frame : frames) {
    periods.push_back(frame.period_ms);
  }
  std::ranges::sort(periods);
  summary.covered_ms = std::accumulate(periods.begin(), periods.end(), 0.0);
  summary.mean_ms = Mean(periods);
  summary.median_ms = Median(periods);
  summary.p99_ms = Percentile(periods, kPeriodPercentile);
  summary.worst_ms = periods.empty() ? 0.0 : periods.back();
  summary.mean_fps = Fps(summary.mean_ms);
  summary.median_fps = Fps(summary.median_ms);
  const auto slowest =
      std::max<size_t>(1, static_cast<size_t>(std::ceil(kLowFraction * static_cast<double>(periods.size()))));
  summary.low_fps = periods.empty() ? 0.0 : Fps(Mean(std::span(periods).last(std::min(slowest, periods.size()))));
  return summary;
}

std::vector<SystemTime> SystemMeans(std::span<const FrameSample> frames, std::span<const std::string> paths) {
  std::vector<double> sums(paths.size());
  size_t counted = 0;
  for (const FrameSample& frame : frames) {
    if (frame.system_ms.size() == paths.size()) {
      ++counted;
      std::ranges::transform(sums, frame.system_ms, sums.begin(), std::plus<>());
    }
  }
  std::vector<SystemTime> means;
  for (size_t i = 0; counted > 0 && i < paths.size(); ++i) {
    means.push_back({.path = paths[i], .ms = sums[i] / static_cast<double>(counted)});
  }
  std::ranges::sort(means, std::greater<>(), &SystemTime::ms);
  return means;
}

std::vector<FrameShare> TimeShares(std::span<const FrameSample> frames) {
  std::vector<FrameShare> shares;
  double period = 0.0;
  for (const FrameSample& frame : frames) {
    period += frame.period_ms;
    for (const FrameStage& part : PartsOf(frame)) {
      const auto found = std::ranges::find(shares, part.name, &FrameShare::name);
      if (found == shares.end()) {
        shares.push_back({.name = std::string(part.name), .ms = part.ms});
      } else {
        found->ms += part.ms;
      }
    }
  }
  const auto count = static_cast<double>(frames.size());
  for (FrameShare& share : shares) {
    share.share = period > 0.0 ? share.ms / period : 0.0;
    share.ms /= std::max(count, 1.0);
  }
  std::ranges::stable_sort(shares, std::greater<>(), &FrameShare::ms);
  return shares;
}

std::string FrameReport(std::span<const FrameSample> frames, const FrameReportInput& input) {
  const FrameSummary summary = SummarizeFrames(frames);
  std::string text;
  auto out = std::back_inserter(text);
  std::format_to(out, "Frame report: {} frames over {:.1f} s\n", summary.frames, summary.covered_ms / kMsPerSecond);
  for (const auto& [name, value] : input.header) {
    std::format_to(out, "{}: {}\n", name, value);
  }
  if (frames.empty()) {
    std::format_to(out, "\nNo frames recorded: play for a few seconds, then open the menu.\n");
  }
  std::format_to(out, "\nFPS: mean {:.1f}, median {:.1f}, slowest 1% {:.1f}\n", summary.mean_fps, summary.median_fps,
                 summary.low_fps);
  std::format_to(out, "Frame: mean {:.2f} ms, median {:.2f} ms, 99th percentile {:.2f} ms, worst {:.2f} ms\n",
                 summary.mean_ms, summary.median_ms, summary.p99_ms, summary.worst_ms);

  std::format_to(out, "\nWhere the time goes (mean ms per frame, share of the period):\n");
  for (const FrameShare& share : TimeShares(frames)) {
    std::format_to(out, "  {:<24} {:7.2f}  {:5.1f}%\n", share.name, share.ms, share.share * kPercent);
  }

  const std::vector<SystemTime> systems = SystemMeans(frames, input.system_paths);
  if (!systems.empty()) {
    std::format_to(out, "\nHeaviest systems (mean ms per frame):\n");
    for (const SystemTime& system : systems | std::views::take(kTopSystems)) {
      if (system.ms >= kMinSystemMs) {
        std::format_to(out, "  {:7.3f}  {}\n", system.ms, system.path);
      }
    }
  }

  if (!input.counters.empty()) {
    std::format_to(out, "\nCounters:\n");
    for (const auto& [name, value] : input.counters) {
      std::format_to(out, "  {}: {}\n", name, value);
    }
  }

  std::vector<size_t> order(frames.size());
  std::iota(order.begin(), order.end(), 0U);
  std::erase_if(order, [&](size_t i) { return frames[i].period_ms <= kSlowFactor * summary.median_ms; });
  std::ranges::stable_sort(order, std::greater<>(), [&](size_t i) { return frames[i].period_ms; });
  if (order.empty()) {
    std::format_to(out, "\nNo frame took over {} times the median.\n", kSlowFactor);
  } else {
    std::format_to(out, "\nSlowest frames, over {} times the median (oldest is #0):\n", kSlowFactor);
  }
  for (const size_t index : order | std::views::take(kSlowestFrames)) {
    const FrameSample& frame = frames[index];
    const std::vector<FrameStage> parts = PartsOf(frame);
    const auto biggest = std::ranges::max_element(parts, {}, &FrameStage::ms);
    std::format_to(out, "  #{:<4} {:7.2f} ms  mostly {} {:.2f} ms ({:.0f}%)\n", index, frame.period_ms, biggest->name,
                   biggest->ms, frame.period_ms > 0.0 ? biggest->ms / frame.period_ms * kPercent : 0.0);
    if (const std::string systems_text = HeaviestSystems(frame, input.system_paths); !systems_text.empty()) {
      std::format_to(out, "        systems: {}\n", systems_text);
    }
  }
  return text;
}

}  // namespace z13
