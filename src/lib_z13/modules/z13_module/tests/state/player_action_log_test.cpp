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

#include <lib_core/simulation_clock.h>
#include <lib_core/world_snapshot_history.h>
#include <z13/components/player_action.h>
#include <z13_tests/test_time.h>

#include "../support/building_test_helpers.h"
#include "../support/z13_test_world.h"

namespace {

namespace ft = z13::flecs_tools;
using z13::gameplay::PlayerActionLog;
using z13::gameplay::PlayerActionRecord;
using z13::testing::kTestDeltaTime;
using z13::testing::KeyDown;
using z13::testing::KeyUp;
using z13::testing::Z13TestWorld;
using Keycode = z13::fbs::input::Keycode;

std::vector<PlayerActionRecord> Records(Z13TestWorld& test_world) {
  const auto& entries = test_world.World().get<PlayerActionLog>().log.Entries();
  return std::vector<PlayerActionRecord>(entries.begin(), entries.end());
}

void Frames(Z13TestWorld& test_world, uint64_t count) {
  for (uint64_t i = 0; i < count; ++i) {
    test_world.World().progress(kTestDeltaTime);
  }
}

uint64_t RoundTicks(double seconds, double ticks_per_second) {
  return static_cast<uint64_t>(std::llround(seconds * ticks_per_second));
}

uint64_t IntervalTicks(Z13TestWorld& test_world) {
  return RoundTicks(test_world.Config().GetSnapshotIntervalSeconds(), test_world.Config().GetFPS());
}

TEST(PlayerActionLogTest, NoRecordsWhileNothingChanges) {
  Z13TestWorld test_world;
  Frames(test_world, 5);
  EXPECT_TRUE(Records(test_world).empty());
}

TEST(PlayerActionLogTest, RecordsPressAndReleaseEdgesOnce) {
  Z13TestWorld test_world;

  test_world.EmitInput(KeyDown(Keycode::KEY_W));
  test_world.World().progress(kTestDeltaTime);

  const auto after_press = Records(test_world);
  ASSERT_EQ(after_press.size(), 1u);
  EXPECT_EQ(after_press.front().value, 1.f);

  test_world.EmitInput(KeyUp(Keycode::KEY_W));
  test_world.World().progress(kTestDeltaTime);

  const auto after_release = Records(test_world);
  ASSERT_EQ(after_release.size(), 2u);
  EXPECT_EQ(after_release[1].action_id, after_press.front().action_id);
  EXPECT_EQ(after_release[1].value, 0.f);
}

TEST(PlayerActionLogTest, HoldingLongerThanOneIntervalAddsNoExtraRecordsBeforeTheBoundary) {
  Z13TestWorld test_world;
  const uint64_t interval_ticks = IntervalTicks(test_world);
  ASSERT_GT(interval_ticks, 2u) << "test assumes room for ticks strictly between press and the boundary";

  test_world.EmitInput(KeyDown(Keycode::KEY_W));
  test_world.World().progress(kTestDeltaTime);
  ASSERT_EQ(Records(test_world).size(), 1u);

  // Hold, stopping one tick short of the next re-assertion boundary.
  Frames(test_world, interval_ticks - 2);

  EXPECT_EQ(Records(test_world).size(), 1u) << "a steady hold must not re-log before the interval boundary";
}

TEST(PlayerActionLogTest, RetentionWindowPrunesRecordsOlderThanItsLimit) {
  Z13TestWorld test_world;
  const uint64_t interval_ticks = IntervalTicks(test_world);
  const uint64_t retention_ticks = RoundTicks(test_world.Config().GetSnapshotRetentionSeconds(), test_world.Config().GetFPS());

  test_world.EmitInput(KeyDown(Keycode::KEY_W));
  test_world.World().progress(kTestDeltaTime);
  ASSERT_FALSE(Records(test_world).empty());

  test_world.EmitInput(KeyUp(Keycode::KEY_W));
  test_world.World().progress(kTestDeltaTime);

  // Run well past the retention window so the press/release above are pruned away.
  Frames(test_world, retention_ticks + interval_ticks);

  for (const auto& record : Records(test_world)) {
    EXPECT_GE(record.tick, test_world.World().get<PlayerActionLog>().retained_since_tick);
  }
}

TEST(PlayerActionLogTest, RetainedSinceTickTracksTheRetentionWindow) {
  Z13TestWorld test_world;
  const uint64_t interval_ticks = IntervalTicks(test_world);
  const uint64_t retention_ticks = RoundTicks(test_world.Config().GetSnapshotRetentionSeconds(), test_world.Config().GetFPS());

  Frames(test_world, retention_ticks + interval_ticks * 2);

  const uint64_t current_tick = test_world.World().get<ft::SimulationClock>().tick;
  const uint64_t retained_since = test_world.World().get<PlayerActionLog>().retained_since_tick;
  EXPECT_EQ(retained_since, current_tick - retention_ticks);
}

TEST(PlayerActionLogTest, HeldActionIsReassertedAtTheSnapshotIntervalBoundary) {
  Z13TestWorld test_world;
  const uint64_t interval_ticks = IntervalTicks(test_world);

  test_world.EmitInput(KeyDown(Keycode::KEY_W));
  test_world.World().progress(kTestDeltaTime);
  const auto action_id = Records(test_world).front().action_id;

  Frames(test_world, interval_ticks - 1);  // now at tick == interval_ticks

  const auto records = Records(test_world);
  ASSERT_EQ(records.size(), 2u) << "expected exactly one re-assertion at the interval boundary";
  EXPECT_EQ(records[1].action_id, action_id);
  EXPECT_EQ(records[1].value, 1.f);
  EXPECT_EQ(records[1].tick, interval_ticks);
}

}  // namespace
