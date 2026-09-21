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

#include <z13/components/building.h>
#include <z13/components/gameplay.h>

#include "../support/building_test_helpers.h"
#include "../support/z13_test_world.h"

// Scene lifecycle around the main menu: Gameplay marks "a scene exists", Pause shows the menu.
namespace z13::gameplay {
namespace {

using z13::building::BasicBlock;
using z13::testing::Click;
using z13::testing::EnterBuildMode;
using z13::testing::kTestDeltaTime;
using z13::testing::Z13TestWorld;

constexpr uint32_t kFirstBlockId = 1;

void PlaceBlock(Z13TestWorld& test_world) {
  EnterBuildMode(test_world);
  Click(test_world, z13::fbs::input::Keycode::MOUSE_BUTTON_LEFT);
}

TEST(MainMenuTest, SkipMainMenuStartsWithSceneImmediately) {
  Z13TestWorld test_world(true);

  EXPECT_TRUE(test_world.World().has<Gameplay>());
  EXPECT_FALSE(test_world.World().has<Pause>());
  EXPECT_TRUE(test_world.Player());
}

TEST(MainMenuTest, NormalLaunchStartsAtMainMenuWithoutScene) {
  Z13TestWorld test_world(false);

  EXPECT_FALSE(test_world.World().has<Gameplay>());
  EXPECT_TRUE(test_world.World().has<Pause>());
  EXPECT_FALSE(test_world.Player());
}

TEST(MainMenuTest, StartGameSpawnsPlayer) {
  Z13TestWorld test_world(false);

  test_world.StartGame();

  EXPECT_TRUE(test_world.Player());
}

TEST(MainMenuTest, ExitToMainMenuDestroysPlayerAndBlocks) {
  Z13TestWorld test_world;
  PlaceBlock(test_world);
  ASSERT_EQ(test_world.World().count<BasicBlock>(), 1);

  test_world.ExitToMainMenu();
  test_world.World().progress(kTestDeltaTime);

  EXPECT_FALSE(test_world.Player());
  EXPECT_EQ(test_world.World().count<BasicBlock>(), 0);
}

TEST(MainMenuTest, RestartedGameGetsFreshIdCounters) {
  Z13TestWorld test_world;
  PlaceBlock(test_world);
  ASSERT_EQ(test_world.World().get<IdCounters>().last_block_id, kFirstBlockId);

  test_world.ExitToMainMenu();
  test_world.World().progress(kTestDeltaTime);
  test_world.StartGame();
  PlaceBlock(test_world);

  EXPECT_EQ(test_world.World().count<BasicBlock>(), 1);
  EXPECT_EQ(test_world.World().get<IdCounters>().last_block_id, kFirstBlockId);
}

}  // namespace
}  // namespace z13::gameplay
