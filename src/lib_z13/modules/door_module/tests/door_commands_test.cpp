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
#include <string>
#include <string_view>

#include <rfl.hpp>
#include <rfl/json.hpp>

#include <z13/components/station/door.h>

#include "../../building_module/tests/support/station_builders.h"
#include "../../z13_module/tests/support/command_ask.h"
#include "../../z13_module/tests/support/z13_test_world.h"

// The door commands set a built door's mode and what it is asked to do.
namespace z13::station {
namespace {

using z13::testing::AskCommand;
using z13::testing::Z13TestWorld;

constexpr std::int64_t kFarCell = 100;  // well clear of the site's slab
constexpr std::string_view kDoorName = "Door";
constexpr std::string_view kManualMode = "kManual";

rfl::Object<rfl::Generic> DoorAt() {
  rfl::Object<rfl::Generic> arguments;
  arguments["primitive"] = rfl::Generic(std::string(kDoorName));
  arguments["x"] = rfl::Generic(kFarCell);
  arguments["y"] = rfl::Generic(kFarCell);
  arguments["z"] = rfl::Generic(kFarCell);
  return arguments;
}

TEST(DoorCommandsTest, ABuiltDoorIsListedAndSet) {
  Z13TestWorld test_world(z13::testing::SiteArgs());
  test_world.Tick();
  ASSERT_TRUE(AskCommand(test_world, "build_block", DoorAt()));
  test_world.Tick();

  const auto listed = AskCommand(test_world, "doors");
  ASSERT_TRUE(listed) << listed.error();
  EXPECT_NE(rfl::json::write(*listed).find("kAuto"), std::string::npos);

  rfl::Object<rfl::Generic> setting = DoorAt();
  setting["mode"] = rfl::Generic(std::string(kManualMode));
  setting["open"] = rfl::Generic(true);
  const auto set = AskCommand(test_world, "set_door", setting);
  ASSERT_TRUE(set) << set.error();

  const auto after = AskCommand(test_world, "doors");
  ASSERT_TRUE(after) << after.error();
  EXPECT_NE(rfl::json::write(*after).find(kManualMode), std::string::npos);
}

TEST(DoorCommandsTest, AnEmptyCellHasNoDoor) {
  Z13TestWorld test_world(z13::testing::SiteArgs());
  test_world.Tick();

  EXPECT_FALSE(AskCommand(test_world, "set_door", DoorAt()));
}

TEST(DoorCommandsTest, AnUnknownModeIsRefusedInWords) {
  Z13TestWorld test_world(z13::testing::SiteArgs());
  test_world.Tick();
  ASSERT_TRUE(AskCommand(test_world, "build_block", DoorAt()));
  test_world.Tick();
  rfl::Object<rfl::Generic> setting = DoorAt();
  setting["mode"] = rfl::Generic(std::string("kNonsense"));

  const auto set = AskCommand(test_world, "set_door", setting);

  ASSERT_FALSE(set);
  EXPECT_NE(set.error().find("kNonsense"), std::string::npos);
}

}  // namespace
}  // namespace z13::station
