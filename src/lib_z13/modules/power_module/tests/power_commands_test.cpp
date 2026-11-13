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

#include "../../building_module/tests/support/station_builders.h"
#include "../../z13_module/tests/support/command_ask.h"
#include "../../z13_module/tests/support/z13_test_world.h"

// The `power_networks` command shows a built reactor feeding a terminal through a cable.
namespace z13::power_module {
namespace {

using z13::testing::AskCommand;
using z13::testing::Z13TestWorld;

constexpr std::string_view kCableKind = "Cable";
constexpr std::int64_t kRow = 100;  // well clear of the site's slab
constexpr std::int64_t kTerminalCell = 107;
constexpr std::int64_t kCableFrom = 104;
constexpr std::int64_t kCableTo = 106;
// Enough ticks for the balance to run and the networks to settle.
constexpr int kBalanceTicks = 30;

rfl::Object<rfl::Generic> BlockAt(std::string_view primitive, std::int64_t x) {
  rfl::Object<rfl::Generic> arguments;
  arguments["primitive"] = rfl::Generic(std::string(primitive));
  arguments["x"] = rfl::Generic(x);
  arguments["y"] = rfl::Generic(kRow);
  arguments["z"] = rfl::Generic(kRow);
  return arguments;
}

TEST(PowerCommandsTest, AReactorFeedsATerminalThroughACable) {
  Z13TestWorld test_world(z13::testing::SiteArgs());
  test_world.Tick();
  ASSERT_TRUE(AskCommand(test_world, "build_block", BlockAt("Reactor", kRow)));
  ASSERT_TRUE(AskCommand(test_world, "build_block", BlockAt("Terminal", kTerminalCell)));
  rfl::Object<rfl::Generic> cable = BlockAt(kCableKind, kCableFrom);
  cable["kind"] = rfl::Generic(std::string(kCableKind));
  cable["to_x"] = rfl::Generic(kCableTo);
  cable["to_y"] = rfl::Generic(kRow);
  cable["to_z"] = rfl::Generic(kRow);
  const auto laid = AskCommand(test_world, "lay_conduit", cable);
  ASSERT_TRUE(laid) << laid.error();
  z13::testing::Ticks(test_world, kBalanceTicks);

  const auto networks = AskCommand(test_world, "power_networks");

  ASSERT_TRUE(networks) << networks.error();
  const std::string text = rfl::json::write(*networks);
  EXPECT_NE(text.find("\"role\":\"producer\""), std::string::npos) << text;
  EXPECT_NE(text.find("\"role\":\"consumer\",\"supplied\":true"), std::string::npos) << text;
}

}  // namespace
}  // namespace z13::power_module
