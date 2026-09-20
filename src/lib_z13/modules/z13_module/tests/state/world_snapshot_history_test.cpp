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

#include <cmath>
#include <cstdint>
#include <vector>

#include <lib_core/world_snapshot_history.h>
#include <z13_tests/test_time.h>

#include "../support/z13_test_world.h"

namespace {

namespace ft = z13::flecs_tools;
using z13::testing::kTestDeltaTime;
using z13::testing::Z13TestWorld;

uint64_t RoundTicks(double seconds, double ticks_per_second) {
  return static_cast<uint64_t>(std::llround(seconds * ticks_per_second));
}

uint64_t IntervalTicks(Z13TestWorld& test_world) {
  return RoundTicks(test_world.Config().GetSnapshotIntervalSeconds(), test_world.Config().GetFPS());
}

void Frames(Z13TestWorld& test_world, uint64_t count) {
  for (uint64_t i = 0; i < count; ++i) {
    test_world.World().progress(kTestDeltaTime);
  }
}

std::vector<uint64_t> CapturedTicks(Z13TestWorld& test_world) {
  std::vector<uint64_t> ticks;
  for (const auto& entry : test_world.World().get<ft::WorldSnapshotHistory>().history.Entries()) {
    ticks.push_back(entry.tick);
  }
  return ticks;
}

TEST(WorldSnapshotHistoryTest, CapturesOnceEveryConfiguredIntervalInTicks) {
  Z13TestWorld test_world;
  const uint64_t interval_ticks = IntervalTicks(test_world);

  Frames(test_world, interval_ticks * 3);

  EXPECT_EQ(CapturedTicks(test_world),
            (std::vector<uint64_t>{interval_ticks, interval_ticks * 2, interval_ticks * 3}));
}

TEST(WorldSnapshotHistoryTest, DoesNotCaptureBetweenIntervals) {
  Z13TestWorld test_world;
  const uint64_t interval_ticks = IntervalTicks(test_world);
  ASSERT_GT(interval_ticks, 1u) << "test assumes at least one tick strictly between captures";

  Frames(test_world, interval_ticks - 1);

  EXPECT_TRUE(CapturedTicks(test_world).empty());
}

TEST(WorldSnapshotHistoryTest, RetentionWindowPrunesSnapshotsOlderThanItsLimit) {
  Z13TestWorld test_world;
  const uint64_t interval_ticks = IntervalTicks(test_world);
  const uint64_t retention_ticks = RoundTicks(test_world.Config().GetSnapshotRetentionSeconds(), test_world.Config().GetFPS());

  // Run well past the retention window so early captures are pruned away.
  Frames(test_world, retention_ticks + interval_ticks * 3);

  const std::vector<uint64_t> ticks = CapturedTicks(test_world);
  ASSERT_FALSE(ticks.empty());
  for (const uint64_t tick : ticks) {
    EXPECT_LE(ticks.back() - tick, retention_ticks);
  }
}

}  // namespace
