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
#include <optional>
#include <string>
#include <string_view>

#include <rfl.hpp>
#include <rfl/json.hpp>

#include <lib_core/commands/command_registry.h>
#include <z13/components/station/conduit.h>

#include "../../z13_module/tests/support/command_ask.h"
#include "../../z13_module/tests/support/z13_test_world.h"
#include "../../building_module/tests/support/station_builders.h"

// The conduit commands lay and remove runs in the main grid, judged as the tool judges them.
namespace z13::conduit {
namespace {

using z13::testing::AskCommand;
using z13::testing::Z13TestWorld;

constexpr std::int64_t kRunStart = 5;
constexpr std::int64_t kRunEnd = 9;
constexpr std::string_view kCable = "Cable";

rfl::Object<rfl::Generic> RunOf(std::string_view kind) {
  rfl::Object<rfl::Generic> arguments;
  arguments["kind"] = rfl::Generic(std::string(kind));
  arguments["x"] = rfl::Generic(kRunStart);
  arguments["y"] = rfl::Generic(kRunStart);
  arguments["z"] = rfl::Generic(kRunStart);
  arguments["to_x"] = rfl::Generic(kRunEnd);
  arguments["to_y"] = rfl::Generic(kRunStart);
  arguments["to_z"] = rfl::Generic(kRunStart);
  return arguments;
}

int RunCount(Z13TestWorld& test_world) {
  return test_world.World().count<Conduit>();
}

TEST(ConduitCommandsTest, KindsListTheCable) {
  Z13TestWorld test_world(z13::testing::SiteArgs());
  test_world.Tick();

  const auto kinds = AskCommand(test_world, "conduit_kinds");
  ASSERT_TRUE(kinds) << kinds.error();
  EXPECT_NE(rfl::json::write(*kinds).find(kCable), std::string::npos);
}

TEST(ConduitCommandsTest, ARunIsLaidListedAndRemoved) {
  Z13TestWorld test_world(z13::testing::SiteArgs());
  test_world.Tick();

  const auto laid = AskCommand(test_world, "lay_conduit", RunOf(kCable));
  ASSERT_TRUE(laid) << laid.error();
  EXPECT_EQ(RunCount(test_world), 1);
  EXPECT_FALSE(AskCommand(test_world, "lay_conduit", RunOf(kCable))) << "the cells hold a power conduit already";

  rfl::Object<rfl::Generic> box;
  box["x"] = rfl::Generic(kRunStart);
  box["y"] = rfl::Generic(kRunStart);
  box["z"] = rfl::Generic(kRunStart);
  const auto listed = AskCommand(test_world, "conduits", box);
  ASSERT_TRUE(listed) << listed.error();
  EXPECT_NE(rfl::json::write(*listed).find(kCable), std::string::npos);

  rfl::Object<rfl::Generic> middle = RunOf(kCable);
  middle["x"] = rfl::Generic((kRunStart + kRunEnd) / 2);
  const auto removed = AskCommand(test_world, "remove_conduit", middle);
  ASSERT_TRUE(removed) << removed.error();
  EXPECT_EQ(RunCount(test_world), 0);
}

TEST(ConduitCommandsTest, AnUnknownKindIsRefusedInWords) {
  Z13TestWorld test_world(z13::testing::SiteArgs());
  test_world.Tick();

  const auto laid = AskCommand(test_world, "lay_conduit", RunOf("Nonsense"));

  ASSERT_FALSE(laid);
  EXPECT_NE(laid.error().find("Nonsense"), std::string::npos);
}

}  // namespace
}  // namespace z13::conduit
