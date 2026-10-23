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

#include <lib_core/utils/frame_recorder.h>

namespace z13 {
namespace {

constexpr std::chrono::milliseconds kWindow {1000};
constexpr double kFrameMs = 100.0;
constexpr double kTolerance = 1e-9;

FrameSample Frame(double period_ms) {
  return {.period_ms = period_ms};
}

TEST(FrameRecorderTest, KeepsTheFramesThatCoverTheWindow) {
  FrameRecorder recorder(kWindow);
  for (int i = 0; i < 25; ++i) {
    recorder.Add(Frame(kFrameMs));
  }

  EXPECT_EQ(recorder.Size(), 10U) << "ten 100 ms frames fill one second";
}

TEST(FrameRecorderTest, KeepsTheNewestFramesAndTheirOrder) {
  FrameRecorder recorder(kWindow);
  for (int i = 0; i < 15; ++i) {
    recorder.Add(Frame(kFrameMs + i));
  }

  const std::vector<FrameSample> frames = recorder.Frames();
  EXPECT_NEAR(frames.back().period_ms, kFrameMs + 14, kTolerance);
  EXPECT_LT(frames.front().period_ms, frames.back().period_ms);
}

TEST(FrameRecorderTest, ALongFrameAloneIsKept) {
  FrameRecorder recorder(kWindow);
  recorder.Add(Frame(5.0 * kWindow.count()));

  EXPECT_EQ(recorder.Size(), 1U);
}

TEST(FrameRecorderTest, ClearEmptiesItButKeepsTheWindow) {
  FrameRecorder recorder(kWindow);
  for (int i = 0; i < 5; ++i) {
    recorder.Add(Frame(kFrameMs));
  }
  recorder.Clear();
  for (int i = 0; i < 25; ++i) {
    recorder.Add(Frame(kFrameMs));
  }

  EXPECT_EQ(recorder.Size(), 10U);
}

}  // namespace
}  // namespace z13
