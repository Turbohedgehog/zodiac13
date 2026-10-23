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

#include <gtest/gtest.h>

#include <algorithm>
#include <string>

#include <lib_core/utils/frame_report.h>

namespace z13 {
namespace {

constexpr double kTolerance = 1e-9;

FrameSample Frame(double period_ms, double simulation_ms = 0.0, double present_ms = 0.0) {
  return {.period_ms = period_ms, .simulation_ms = simulation_ms, .present_ms = present_ms};
}

bool Contains(const std::string& text, const std::string& part) {
  return text.find(part) != std::string::npos;
}

TEST(FrameReportTest, SummarizesPeriodsAndFps) {
  // 100 frames: 98 of 10 ms, one of 20 ms and one of 50 ms.
  std::vector<FrameSample> frames(98, Frame(10.0));
  frames.push_back(Frame(20.0));
  frames.push_back(Frame(50.0));

  const FrameSummary summary = SummarizeFrames(frames);
  EXPECT_EQ(summary.frames, 100U);
  EXPECT_NEAR(summary.covered_ms, 1050.0, kTolerance);
  EXPECT_NEAR(summary.mean_ms, 10.5, kTolerance);
  EXPECT_NEAR(summary.median_ms, 10.0, kTolerance);
  EXPECT_NEAR(summary.p99_ms, 20.0, kTolerance);
  EXPECT_NEAR(summary.worst_ms, 50.0, kTolerance);
  EXPECT_NEAR(summary.median_fps, 100.0, kTolerance);
  EXPECT_NEAR(summary.low_fps, 20.0, kTolerance) << "the slowest 1% of 100 frames is the 50 ms one";
}

TEST(FrameReportTest, NoFramesSummarizeToZeros) {
  const FrameSummary summary = SummarizeFrames({});

  EXPECT_EQ(summary.frames, 0U);
  EXPECT_NEAR(summary.mean_fps, 0.0, kTolerance);
  EXPECT_NEAR(summary.low_fps, 0.0, kTolerance);
  EXPECT_FALSE(FrameReport({}, {}).empty());
}

TEST(FrameReportTest, TimeSharesNameWhereTheFrameWent) {
  FrameSample frame = Frame(20.0, 4.0, 6.0);
  frame.stages = {{.name = "opaque", .ms = 8.0}, {.name = "glass", .ms = 1.0}};

  const std::vector<FrameShare> shares = TimeShares(std::vector {frame, frame});
  ASSERT_EQ(shares.size(), 5U);
  EXPECT_EQ(shares[0].name, "opaque");
  EXPECT_NEAR(shares[0].ms, 8.0, kTolerance);
  EXPECT_NEAR(shares[0].share, 0.4, kTolerance);
  const auto rest = std::ranges::find(shares, std::string("rest (GUI, waiting)"), &FrameShare::name);
  ASSERT_NE(rest, shares.end());
  EXPECT_NEAR(rest->ms, 1.0, kTolerance) << "20 - (4 + 8 + 1 + 6)";
}

TEST(FrameReportTest, ThePartsNeverMakeTheRestNegative) {
  FrameSample frame = Frame(10.0, 8.0, 8.0);

  for (const FrameShare& share : TimeShares(std::vector {frame})) {
    EXPECT_GE(share.ms, 0.0) << share.name;
  }
}

TEST(FrameReportTest, ReportsHeaderCountersSystemsAndSlowFrames) {
  std::vector<FrameSample> frames(20, Frame(10.0, 2.0, 1.0));
  frames[7] = Frame(80.0, 70.0, 1.0);
  const FrameReportInput input {
      .header = {{"Device", "test phone"}},
      .counters = {{"Chunks", "98 / 117"}},
      .systems = {{.path = "light", .ms = 0.5}, {.path = "physics", .ms = 1.75}},
  };

  const std::string report = FrameReport(frames, input);
  EXPECT_TRUE(Contains(report, "Frame report: 20 frames"));
  EXPECT_TRUE(Contains(report, "Device: test phone"));
  EXPECT_TRUE(Contains(report, "Chunks: 98 / 117"));
  EXPECT_TRUE(Contains(report, "#7 "));
  EXPECT_TRUE(Contains(report, "mostly simulation 70.00 ms (88%)"));
  EXPECT_LT(report.find("physics"), report.find("light")) << "the heavier system first";
}

TEST(FrameReportTest, TheSlowestFramesAreListedLongestFirstAndCapped) {
  std::vector<FrameSample> frames(100, Frame(10.0));
  for (int i = 0; i < 30; ++i) {
    frames.push_back(Frame(100.0 + i));
  }

  const std::string report = FrameReport(frames, {});
  EXPECT_LT(report.find("#129 "), report.find("#128 "));
  EXPECT_EQ(report.find("#110 "), std::string::npos);
  EXPECT_EQ(report.find("#5 "), std::string::npos) << "an ordinary frame isn't listed";
}

TEST(FrameReportTest, SaysSoWhenNoFrameStoodOut) {
  const std::string report = FrameReport(std::vector<FrameSample>(20, Frame(10.0)), {});

  EXPECT_TRUE(Contains(report, "No frame took over"));
  EXPECT_FALSE(Contains(report, "Slowest frames"));
}

}  // namespace
}  // namespace z13
