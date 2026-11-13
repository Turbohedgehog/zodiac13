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

#include <string>

#include <rfl.hpp>
#include <rfl/json.hpp>

#include "../../building_module/tests/support/station_builders.h"
#include "../../z13_module/tests/support/command_ask.h"
#include "../../z13_module/tests/support/z13_test_world.h"
#include "support/room_blocks.h"

// The `rooms` command tells a builder whether the walls closed a room.
namespace z13::station {
namespace {

using testing::AddBlocks;
using testing::SealedBox;
using z13::testing::AskCommand;
using z13::testing::Z13TestWorld;

constexpr std::string_view kLeaks = "\"open_to_space\":true";
constexpr std::string_view kSealed = "\"open_to_space\":false";
constexpr int kSettleFrames = 3;

TEST(RoomCommandsTest, ASealedBoxIsOneRoomThatDoesNotLeak) {
  Z13TestWorld test_world(z13::testing::SiteArgs());
  AddBlocks(test_world, SealedBox());
  z13::testing::Ticks(test_world, kSettleFrames);

  const auto rooms = AskCommand(test_world, "rooms");

  ASSERT_TRUE(rooms) << rooms.error();
  const std::string text = rfl::json::write(*rooms);
  EXPECT_NE(text.find(kSealed), std::string::npos) << text;
  EXPECT_EQ(text.find(kLeaks), std::string::npos) << text;
}

}  // namespace
}  // namespace z13::station
