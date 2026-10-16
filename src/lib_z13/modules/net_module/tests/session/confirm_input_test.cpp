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
#include <cstdint>
#include <optional>

#include <lib_core/state/rollback.h>

#include <z13/components/action_map.h>
#include <z13/components/player_action.h>

#include "../../../z13_module/tests/support/z13_test_world.h"
#include "../../src/remote_input.h"

// A confirmation turns a remote player's ticks from predicted (released) into read from the log.
namespace z13::net {
namespace {

namespace ft = z13::flecs_tools;
using z13::testing::Z13TestWorld;

constexpr uint32_t kRemotePlayer = 7;
// Past the snapshot retention, so the oldest snapshot is well after tick 0.
constexpr uint64_t kRunTicks = 600;
// Ticks between a stale confirmation, a record and the next confirmation.
constexpr uint64_t kStep = 10;

class ConfirmInputThroughTest : public ::testing::Test {
 protected:
  void SetUp() override {
    for (uint64_t tick = 0; tick < kRunTicks; ++tick) {
      world_.Tick();
    }
    const std::optional<uint64_t> oldest = OldestSnapshotTick(world_.World());
    ASSERT_TRUE(oldest && *oldest > kStep);
    oldest_ = *oldest;
    ASSERT_FALSE(ft::DeferredRollbackTick(world_.World()));
  }

  void ConfirmedThrough(uint64_t tick) {
    world_.World().get_mut<z13::gameplay::ConfirmedInputTicks>().by_player[kRemotePlayer] = tick;
  }

  void Record(uint64_t tick) {
    // A key, not a look angle: only those are released.
    const auto& actions = world_.World().get<z13::input::ActionMap>().action_map.get<z13::input::ActionMap::IdTag>();
    const auto key = std::ranges::find_if(actions, [](const auto& action) { return !action.absolute; });
    ASSERT_NE(key, actions.end());
    world_.World().get_mut<z13::gameplay::PlayerActionLog>().log.MergeSorted(
        {{.tick = tick, .player_id = kRemotePlayer, .action_id = key->id, .value = 1.f}}, z13::gameplay::RecordLess);
  }

  Z13TestWorld world_;
  uint64_t oldest_ {};
};

// A command the server moved past the old confirmation read as released until now.
TEST_F(ConfirmInputThroughTest, ReplaysRecordsTheConfirmationUncovers) {
  ConfirmedThrough(oldest_ + kStep);
  Record(oldest_ + 2 * kStep);

  EXPECT_TRUE(ConfirmInputThrough(world_.World(), kRemotePlayer, oldest_ + 3 * kStep));
  EXPECT_EQ(ft::DeferredRollbackTick(world_.World()), oldest_ + kStep);
}

TEST_F(ConfirmInputThroughTest, LeavesThePastAloneWhenNothingChanges) {
  ConfirmedThrough(oldest_ + kStep);

  EXPECT_TRUE(ConfirmInputThrough(world_.World(), kRemotePlayer, oldest_ + 3 * kStep));
  EXPECT_FALSE(ft::DeferredRollbackTick(world_.World()));
}

// A player stalled since before the oldest snapshot.
TEST_F(ConfirmInputThroughTest, ReleasesNoEarlierThanTheOldestSnapshot) {
  ConfirmedThrough(oldest_ - kStep);
  Record(oldest_ - kStep);

  const auto releases = ReleasesAfter(world_.World(), kRemotePlayer, oldest_ - kStep);

  ASSERT_EQ(releases.size(), 1u);
  EXPECT_EQ(releases.front().tick, oldest_ + 1);
  EXPECT_EQ(releases.front().value, 0.f);
}

TEST_F(ConfirmInputThroughTest, RollsBackNoFurtherThanTheOldestSnapshot) {
  ConfirmedThrough(oldest_ - kStep);
  Record(oldest_ + kStep);

  EXPECT_TRUE(ConfirmInputThrough(world_.World(), kRemotePlayer, oldest_ + 2 * kStep));
  EXPECT_EQ(ft::DeferredRollbackTick(world_.World()), oldest_);
}

}  // namespace
}  // namespace z13::net
