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

#include <chrono>
#include <cstdint>

#include <lib_core/tick_pacer.h>

namespace z13 {
namespace {

using namespace std::chrono_literals;

constexpr TickPacer::Clock::duration kTick = 10ms;
const TickPacer::Clock::time_point kStart {};
constexpr uint64_t kMaxBacklog = 15;

uint64_t TakeAllDue(TickPacer& pacer, TickPacer::Clock::time_point now) {
  uint64_t ticks {};
  while (pacer.TakeTick(now)) {
    ++ticks;
  }
  return ticks;
}

TEST(TickPacerTest, RunsOneTickPerTickDuration) {
  TickPacer pacer(kTick, kStart, kMaxBacklog);

  EXPECT_EQ(TakeAllDue(pacer, kStart), 1u);
  EXPECT_EQ(TakeAllDue(pacer, kStart + kTick / 2), 0u);
  EXPECT_EQ(TakeAllDue(pacer, kStart + kTick), 1u);
  EXPECT_EQ(pacer.NextTickTime(), kStart + 2 * kTick);
}

TEST(TickPacerTest, SlowFrameIsMadeUpByTheNextTicks) {
  TickPacer pacer(kTick, kStart, kMaxBacklog);
  ASSERT_EQ(TakeAllDue(pacer, kStart), 1u);

  EXPECT_EQ(TakeAllDue(pacer, kStart + 3 * kTick + kTick / 2), 3u);
  EXPECT_EQ(pacer.NextTickTime(), kStart + 4 * kTick);
}

TEST(TickPacerTest, HitchBacklogIsDropped) {
  TickPacer pacer(kTick, kStart, kMaxBacklog);
  ASSERT_EQ(TakeAllDue(pacer, kStart), 1u);
  const auto after_hitch = kStart + 100 * kTick;

  EXPECT_EQ(TakeAllDue(pacer, after_hitch), 1u);
  EXPECT_EQ(pacer.NextTickTime(), after_hitch + kTick);
}

}  // namespace
}  // namespace z13
