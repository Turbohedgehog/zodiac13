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

#include <vector>

#include <lib_core/utils/frame_timeline.h>

namespace z13 {
namespace {

using Clock = FrameTimeline::Clock;
using std::chrono::milliseconds;

constexpr double kTolerance = 1e-9;
constexpr milliseconds kFrame {16};

const std::vector<double> kNoSystems;

// Starts the next frame `ms` after `from`; returns when it started.
Clock::time_point Next(FrameTimeline& timeline, Clock::time_point from, milliseconds ms, bool paused = false,
                       const std::vector<FrameStage>& stages = {}, const std::vector<double>& totals = kNoSystems) {
  const Clock::time_point now = from + ms;
  timeline.StartFrame(now, paused, stages, totals);
  return now;
}

TEST(FrameTimelineTest, RecordsAFrameWhenTheNextOneStarts) {
  FrameTimeline timeline;
  const Clock::time_point start = Clock::now();
  timeline.StartFrame(start, false, {}, kNoSystems);
  EXPECT_EQ(timeline.Recorder().Size(), 0U) << "the first frame is still running";

  Next(timeline, start, kFrame);

  ASSERT_EQ(timeline.Recorder().Size(), 1U);
  EXPECT_NEAR(timeline.Recorder().Frames().front().period_ms, 16.0, kTolerance);
}

TEST(FrameTimelineTest, ADrawnFrameSplitsIntoSimulationDrawingAndSwap) {
  FrameTimeline timeline;
  const Clock::time_point start = Clock::now();
  timeline.StartFrame(start, false, {}, kNoSystems);
  timeline.MarkDrawStart(start + milliseconds(5));
  timeline.MarkPresent(7.5);

  const std::vector<FrameStage> stages {{.name = "opaque", .ms = 4.0}};
  Next(timeline, start, kFrame, false, stages);

  const FrameSample frame = timeline.Recorder().Frames().front();
  EXPECT_NEAR(frame.simulation_ms, 5.0, kTolerance);
  EXPECT_NEAR(frame.present_ms, 7.5, kTolerance);
  ASSERT_EQ(frame.stages.size(), 1U);
  EXPECT_EQ(frame.stages.front().name, "opaque");
}

TEST(FrameTimelineTest, AFrameThatWasntDrawnIsAllSimulation) {
  FrameTimeline timeline;
  const Clock::time_point start = Clock::now();
  timeline.StartFrame(start, false, {}, kNoSystems);
  timeline.MarkPresent(3.0);
  Next(timeline, start, kFrame, false, {{.name = "opaque", .ms = 4.0}});

  const FrameSample frame = timeline.Recorder().Frames().front();
  EXPECT_NEAR(frame.simulation_ms, 16.0, kTolerance);
  EXPECT_NEAR(frame.present_ms, 0.0, kTolerance);
  EXPECT_TRUE(frame.stages.empty());
}

TEST(FrameTimelineTest, MarksOfOneFrameDontLeakIntoTheNext) {
  FrameTimeline timeline;
  Clock::time_point now = Clock::now();
  timeline.StartFrame(now, false, {}, kNoSystems);
  timeline.MarkDrawStart(now + milliseconds(2));
  timeline.MarkPresent(9.0);
  now = Next(timeline, now, kFrame);
  Next(timeline, now, kFrame);

  const FrameSample second = timeline.Recorder().Frames().back();
  EXPECT_NEAR(second.present_ms, 0.0, kTolerance);
  EXPECT_NEAR(second.simulation_ms, 16.0, kTolerance);
}

TEST(FrameTimelineTest, PausedFramesAreNotRecordedAndEndingThePauseStartsOver) {
  FrameTimeline timeline;
  Clock::time_point now = Clock::now();
  timeline.StartFrame(now, false, {}, kNoSystems);
  now = Next(timeline, now, kFrame);
  now = Next(timeline, now, kFrame);
  ASSERT_EQ(timeline.Recorder().Size(), 2U);

  now = Next(timeline, now, kFrame, true);
  EXPECT_EQ(timeline.Recorder().Size(), 3U) << "the frame the pause began in ends as any frame";
  now = Next(timeline, now, kFrame, true);
  now = Next(timeline, now, kFrame, true);
  EXPECT_EQ(timeline.Recorder().Size(), 3U) << "the menu's frames are left out";

  now = Next(timeline, now, kFrame, false);
  EXPECT_EQ(timeline.Recorder().Size(), 0U) << "the game before the pause is dropped";
  Next(timeline, now, kFrame, false);
  EXPECT_EQ(timeline.Recorder().Size(), 1U);
}

TEST(FrameTimelineTest, AFrameGetsWhatEachSystemSpentDuringIt) {
  FrameTimeline timeline;
  Clock::time_point now = Clock::now();
  timeline.StartFrame(now, false, {}, std::vector<double> {1.0, 2.0});
  Next(timeline, now, kFrame, false, {}, std::vector<double> {4.0, 9.0});

  const std::vector<float> spent = timeline.Recorder().Frames().front().system_ms;
  ASSERT_EQ(spent.size(), 2U);
  EXPECT_NEAR(spent[0], 3.0, kTolerance);
  EXPECT_NEAR(spent[1], 7.0, kTolerance);
}

TEST(FrameTimelineTest, WithoutComparableTotalsAFrameKnowsNoSystems) {
  FrameTimeline timeline;
  Clock::time_point now = Clock::now();
  timeline.StartFrame(now, false, {}, std::vector<double> {1.0, 2.0});
  now = Next(timeline, now, kFrame, false, {}, std::vector<double> {4.0, 9.0, 1.0});
  Next(timeline, now, kFrame, false, {}, std::vector<double> {5.0, 9.0, 2.0});

  const std::vector<FrameSample> frames = timeline.Recorder().Frames();
  ASSERT_EQ(frames.size(), 2U);
  EXPECT_TRUE(frames[0].system_ms.empty()) << "a system appeared";
  EXPECT_EQ(frames[1].system_ms.size(), 3U);
}

TEST(FrameTimelineTest, CountsWhatItSawAndLeftOut) {
  FrameTimeline timeline;
  Clock::time_point now = Clock::now();
  timeline.StartFrame(now, false, {}, kNoSystems);
  now = Next(timeline, now, kFrame);
  now = Next(timeline, now, kFrame, true);
  now = Next(timeline, now, kFrame, true);
  now = Next(timeline, now, kFrame, false);

  EXPECT_EQ(timeline.Counts().started, 5U);
  EXPECT_EQ(timeline.Counts().paused, 2U);
  EXPECT_EQ(timeline.Counts().cleared, 1U) << "the end of the pause";
}

TEST(FrameTimelineTest, ClearForgetsTheFramesAndTheSystems) {
  FrameTimeline timeline;
  Clock::time_point now = Clock::now();
  timeline.StartFrame(now, false, {}, kNoSystems);
  now = Next(timeline, now, kFrame);
  timeline.Clear();

  EXPECT_EQ(timeline.Recorder().Size(), 0U);
}

}  // namespace
}  // namespace z13
