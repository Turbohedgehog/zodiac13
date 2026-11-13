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

#include <optional>
#include <string>

#include <rfl.hpp>
#include <rfl/json.hpp>

#include <lib_core/commands/command_registry.h>
#include <z13/components/station/block.h>

#include "../../z13_module/tests/support/command_ask.h"
#include "../../z13_module/tests/support/z13_test_world.h"
#include "support/station_builders.h"

// The build commands edit the main grid at once and judge a build by ValidateBuild, as a player's does.
namespace z13::building {
namespace {

using z13::testing::AskCommand;
using z13::testing::Z13TestWorld;

constexpr int kFarCell = 100;  // well clear of the site's slab
constexpr std::string_view kCubeName = "Cube";

rfl::Object<rfl::Generic> CubeAt(int x, int y, int z) {
  rfl::Object<rfl::Generic> arguments;
  arguments["primitive"] = rfl::Generic(std::string(kCubeName));
  arguments["x"] = rfl::Generic(static_cast<std::int64_t>(x));
  arguments["y"] = rfl::Generic(static_cast<std::int64_t>(y));
  arguments["z"] = rfl::Generic(static_cast<std::int64_t>(z));
  return arguments;
}

TEST(BuildCommandsTest, PaletteListsTheCube) {
  Z13TestWorld test_world(z13::testing::SiteArgs());
  test_world.Tick();

  const auto palette = AskCommand(test_world, "palette");
  ASSERT_TRUE(palette) << palette.error();
  EXPECT_NE(rfl::json::write(*palette).find(kCubeName), std::string::npos);
}

TEST(BuildCommandsTest, ABlockIsBuiltListedAndDestroyed) {
  Z13TestWorld test_world(z13::testing::SiteArgs());
  test_world.Tick();
  const int before = test_world.World().count<z13::station::Block>();

  const auto built = AskCommand(test_world, "build_block", CubeAt(kFarCell, kFarCell, kFarCell));
  ASSERT_TRUE(built) << built.error();
  EXPECT_EQ(test_world.World().count<z13::station::Block>(), before + 1);

  rfl::Object<rfl::Generic> box;
  box["x"] = rfl::Generic(static_cast<std::int64_t>(kFarCell));
  box["y"] = rfl::Generic(static_cast<std::int64_t>(kFarCell));
  box["z"] = rfl::Generic(static_cast<std::int64_t>(kFarCell));
  const auto listed = AskCommand(test_world, "blocks", box);
  ASSERT_TRUE(listed) << listed.error();
  EXPECT_NE(rfl::json::write(*listed).find(kCubeName), std::string::npos);

  EXPECT_FALSE(AskCommand(test_world, "build_block", CubeAt(kFarCell, kFarCell, kFarCell))) << "the cells are taken";

  const auto destroyed = AskCommand(test_world, "destroy_block", box);
  ASSERT_TRUE(destroyed) << destroyed.error();
  EXPECT_EQ(test_world.World().count<z13::station::Block>(), before);
}

TEST(BuildCommandsTest, AnUnknownPrimitiveIsRefusedInWords) {
  Z13TestWorld test_world(z13::testing::SiteArgs());
  test_world.Tick();
  auto arguments = CubeAt(0, 0, kFarCell);
  arguments["primitive"] = rfl::Generic(std::string("Nonsense"));

  const auto built = AskCommand(test_world, "build_block", arguments);

  ASSERT_FALSE(built);
  EXPECT_NE(built.error().find("Nonsense"), std::string::npos);
}

TEST(BuildCommandsTest, WithoutAStationNothingIsBuilt) {
  Z13TestWorld test_world;
  test_world.Tick();

  EXPECT_FALSE(AskCommand(test_world, "build_block", CubeAt(0, 0, kFarCell)));
}

}  // namespace
}  // namespace z13::building
