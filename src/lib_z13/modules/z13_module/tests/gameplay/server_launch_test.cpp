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

#include <z13/components/gameplay.h>
#include <z13/components/net.h>

#include "../support/z13_test_world.h"

// --server / --connect bootstrap behavior (docs/client-server-plan.md, stage 1). No
// networking yet: --server jumps straight to Gameplay, reusing the existing
// single-player spawn path as-is; --connect only marks the role for now, the join
// itself is a later branch.
namespace z13::net {
namespace {

using z13::gameplay::Gameplay;
using z13::gameplay::Pause;
using z13::testing::Z13TestWorld;

TEST(ServerLaunchTest, DefaultRoleIsNeitherServerNorClient) {
  Z13TestWorld test_world(/*skip_main_menu=*/false);

  EXPECT_FALSE(test_world.World().has<ServerRole>());
  EXPECT_FALSE(test_world.World().has<ClientRole>());
}

TEST(ServerLaunchTest, ServerFlagStartsGameplayWithoutPauseAndSetsServerRole) {
  Z13TestWorld test_world(/*skip_main_menu=*/false, {"--server"});

  EXPECT_TRUE(test_world.World().has<Gameplay>());
  EXPECT_FALSE(test_world.World().has<Pause>());
  EXPECT_TRUE(test_world.World().has<ServerRole>());
  EXPECT_FALSE(test_world.World().has<ClientRole>());
  ASSERT_TRUE(test_world.Player());
  EXPECT_EQ(test_world.Player().get<z13::gameplay::Player>().id, 0u);
}

TEST(ServerLaunchTest, ConnectFlagSetsClientRoleButStaysAtMainMenuForNow) {
  Z13TestWorld test_world(/*skip_main_menu=*/false, {"--connect", "127.0.0.1:26213"});

  EXPECT_FALSE(test_world.World().has<Gameplay>());
  EXPECT_TRUE(test_world.World().has<Pause>());
  EXPECT_TRUE(test_world.World().has<ClientRole>());
  EXPECT_FALSE(test_world.World().has<ServerRole>());
}

}  // namespace
}  // namespace z13::net
