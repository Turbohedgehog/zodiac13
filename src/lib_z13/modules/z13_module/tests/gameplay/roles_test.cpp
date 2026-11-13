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
#include <vector>

#include <player/roles.h>
#include <z13/components/gameplay/role.h>

// Posts of the crew: roles.json and the server's choice among the posts a player asks for.
namespace z13::gameplay {
namespace {

using z13::inventory::Access;
using z13::inventory::AccessBit;
using z13::inventory::Roles;

Roles TwoPosts() {
  const auto roles = ParseRoles(R"({"roles": [
      {"name": "captain", "slots": 1, "access": ["bridge", "armory"]},
      {"name": "assistant", "access": []}]})");
  EXPECT_TRUE(roles.has_value()) << (roles ? "" : roles.error());
  return roles.value_or(Roles {});
}

TEST(RolesTest, AParsedPostHasTheAccessesItNames) {
  const Roles roles = TwoPosts();
  ASSERT_EQ(roles.roles.size(), 2u);
  EXPECT_EQ(roles.roles.front().access, AccessBit(Access::kBridge) | AccessBit(Access::kArmory));
  EXPECT_EQ(roles.roles.front().slots, 1u);
}

TEST(RolesTest, ABadFileIsRefused) {
  EXPECT_FALSE(ParseRoles(R"({"roles": [{"name": "captain", "slots": 1}]})")) << "every post is limited";
  EXPECT_FALSE(ParseRoles(R"({"roles": [{"name": "a"}, {"name": "a"}]})")) << "a name repeats";
  EXPECT_FALSE(ParseRoles(R"({"roles": [{"name": "a", "access": ["wizardry"]}]})")) << "no such access";
}

TEST(RolesTest, AnEmptyTableHasNoPostWithoutALimit) {
  EXPECT_FALSE(ParseRoles(R"({"roles": []})"));
}

TEST(RolesTest, ThePlayerGetsTheBestPostWithAPlaceLeft) {
  const Roles roles = TwoPosts();
  const std::vector<std::string> desired {"captain", "assistant"};

  EXPECT_EQ(AssignRole(roles, desired, {}).name, "captain");
  const std::vector<std::string> taken {"captain"};
  EXPECT_EQ(AssignRole(roles, desired, taken).name, "assistant") << "the captain's place is taken";
}

TEST(RolesTest, ThePlayerAskingForNothingKnownGetsThePostWithoutALimit) {
  const Roles roles = TwoPosts();
  const std::vector<std::string> desired {"astronaut"};
  EXPECT_EQ(AssignRole(roles, desired, {}).name, "assistant");
  EXPECT_EQ(AssignRole(roles, {}, {}).name, "assistant");
}

TEST(RolesTest, ANamesListIsSplitByCommas) {
  const std::vector<std::string> expected {"captain", "engineer"};
  EXPECT_EQ(SplitRoleNames(" captain, engineer ,,"), expected);
}

}  // namespace
}  // namespace z13::gameplay
