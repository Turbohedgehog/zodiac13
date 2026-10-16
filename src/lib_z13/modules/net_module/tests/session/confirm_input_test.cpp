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

#include <cstdint>

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
constexpr uint64_t kStaleThrough = 10;
constexpr uint64_t kMovedCommandTick = 20;
constexpr uint64_t kConfirmedThrough = 40;
// Past every tick above, so they are all in the past of the world.
constexpr uint64_t kWarmUpTicks = 60;

class ConfirmInputThroughTest : public ::testing::Test {
 protected:
  void SetUp() override {
    for (uint64_t tick = 0; tick < kWarmUpTicks; ++tick) {
      world_.Tick();
    }
    world_.World().get_mut<z13::gameplay::ConfirmedInputTicks>().by_player[kRemotePlayer] = kStaleThrough;
    ASSERT_FALSE(ft::DeferredRollbackTick(world_.World()));
  }

  void Record(uint64_t tick) {
    const auto& actions = world_.World().get<z13::input::ActionMap>().action_map.get<z13::input::ActionMap::IdTag>();
    ASSERT_FALSE(actions.empty());
    world_.World().get_mut<z13::gameplay::PlayerActionLog>().log.MergeSorted(
        {{.tick = tick, .player_id = kRemotePlayer, .action_id = actions.begin()->id, .value = 1.f}},
        z13::gameplay::RecordLess);
  }

  Z13TestWorld world_;
};

// A command the server moved past the old confirmation read as released until now.
TEST_F(ConfirmInputThroughTest, ReplaysRecordsTheConfirmationUncovers) {
  Record(kMovedCommandTick);

  EXPECT_TRUE(ConfirmInputThrough(world_.World(), kRemotePlayer, kConfirmedThrough));
  EXPECT_EQ(ft::DeferredRollbackTick(world_.World()), kStaleThrough);
}

TEST_F(ConfirmInputThroughTest, LeavesThePastAloneWhenNothingChanges) {
  EXPECT_TRUE(ConfirmInputThrough(world_.World(), kRemotePlayer, kConfirmedThrough));
  EXPECT_FALSE(ft::DeferredRollbackTick(world_.World()));
}

}  // namespace
}  // namespace z13::net
