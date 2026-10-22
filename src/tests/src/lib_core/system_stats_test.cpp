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


#include <algorithm>

#include <gtest/gtest.h>

#include <lib_core/utils/system_stats.h>

namespace z13 {
namespace {

constexpr double kTolerance = 1e-9;

const SystemSummary& Find(const std::vector<SystemSummary>& summary, const std::string& path) {
  return *std::ranges::find(summary, path, &SystemSummary::path);
}

TEST(SystemFrameStatsTest, SummarizesEachSystemsFrames) {
  SystemFrameStats stats;
  // "a" spends 1, 4, 2 and 9 ms in four frames: mean 4, median 3.
  double a = 0.0;
  for (const double frame : {1.0, 4.0, 2.0, 9.0}) {
    const std::vector<SystemTime> before {{.path = "a", .ms = a}};
    a += frame;
    stats.AddFrame(before, {{.path = "a", .ms = a}});
  }
  EXPECT_EQ(stats.Frames(), 4);
  const SystemSummary& summary = Find(stats.Summary(), "a");
  EXPECT_NEAR(summary.mean_ms, 4.0, kTolerance);
  EXPECT_NEAR(summary.min_ms, 1.0, kTolerance);
  EXPECT_NEAR(summary.max_ms, 9.0, kTolerance);
  EXPECT_NEAR(summary.median_ms, 3.0, kTolerance);
}

TEST(SystemFrameStatsTest, AnOddCountTakesTheMiddleFrame) {
  SystemFrameStats stats;
  double ms = 0.0;
  for (const double frame : {5.0, 1.0, 3.0}) {
    const std::vector<SystemTime> before {{.path = "a", .ms = ms}};
    ms += frame;
    stats.AddFrame(before, {{.path = "a", .ms = ms}});
  }
  EXPECT_NEAR(Find(stats.Summary(), "a").median_ms, 3.0, kTolerance);
}

TEST(SystemFrameStatsTest, ASystemFirstSeenLateCountsZeroForTheFramesBefore) {
  SystemFrameStats stats;
  stats.AddFrame({}, {});
  stats.AddFrame({}, {});
  stats.AddFrame({}, {{.path = "late", .ms = 6.0}});
  const SystemSummary& summary = Find(stats.Summary(), "late");
  EXPECT_NEAR(summary.mean_ms, 2.0, kTolerance);
  EXPECT_NEAR(summary.min_ms, 0.0, kTolerance);
  EXPECT_NEAR(summary.max_ms, 6.0, kTolerance);
  EXPECT_NEAR(summary.median_ms, 0.0, kTolerance);
}

TEST(SystemFrameStatsTest, HeaviestMeanComesFirst) {
  SystemFrameStats stats;
  stats.AddFrame({}, {{.path = "light", .ms = 1.0}, {.path = "heavy", .ms = 5.0}});
  const std::vector<SystemSummary> summary = stats.Summary();
  ASSERT_EQ(summary.size(), 2u);
  EXPECT_EQ(summary.front().path, "heavy");
}

}  // namespace
}  // namespace z13
