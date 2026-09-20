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

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include <Eigen/Dense>

#include <lib_core/math.h>
#include <lib_core/world_json_store.h>

#include <z13/components/building.h>

#include "../support/building_test_helpers.h"
#include "../support/z13_test_world.h"

// F5 / F9 quick save and load, driven through the same input pipeline as the game.
namespace z13::state {
namespace {

namespace ft = z13::flecs_tools;
using z13::building::BasicBlock;
using z13::testing::Click;
using z13::testing::kTestDeltaTime;
using z13::testing::Z13TestWorld;
using Keycode = z13::fbs::input::Keycode;

// Blocks are placed at the brush, this far in front of the player (+X).
constexpr float kBrushDistance = 5.f;
constexpr float kBlockSpacing = 6.f;

void BuildBlock(Z13TestWorld& test_world, int index) {
  Eigen::Matrix4f player_transform = Eigen::Matrix4f::Identity();
  z13::math::SetTranslation(
      Eigen::Vector3f(-kBrushDistance, kBlockSpacing * static_cast<float>(index), 0.f), player_transform);
  test_world.Player().set(player_transform);
  test_world.World().progress(kTestDeltaTime);
  Click(test_world, Keycode::MOUSE_BUTTON_LEFT);
}

// The request is filed in the frame the key goes down and executed at the start of the next.
void Tap(Z13TestWorld& test_world, Keycode key) {
  test_world.EmitInput(z13::testing::KeyDown(key));
  test_world.World().progress(kTestDeltaTime);
  test_world.World().progress(kTestDeltaTime);
  test_world.EmitInput(z13::testing::KeyUp(key));
  test_world.World().progress(kTestDeltaTime);
}

size_t Blocks(Z13TestWorld& test_world) {
  return static_cast<size_t>(test_world.World().count<BasicBlock>());
}

std::string ReadFile(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

TEST(QuickSaveTest, F5WritesTheSceneToTheQuickSaveFile) {
  Z13TestWorld test_world;
  z13::testing::EnterBuildMode(test_world);
  BuildBlock(test_world, 0);
  BuildBlock(test_world, 1);
  ASSERT_FALSE(std::filesystem::exists(test_world.QuickSavePath()));

  Tap(test_world, Keycode::KEY_F5);

  ASSERT_TRUE(std::filesystem::exists(test_world.QuickSavePath()));
  EXPECT_EQ(ReadFile(test_world.QuickSavePath()), ft::WorldJsonStore::Save(test_world.World()).value());
}

TEST(QuickSaveTest, F9RestoresTheSceneInBuildMode) {
  Z13TestWorld test_world;
  z13::testing::EnterBuildMode(test_world);
  BuildBlock(test_world, 0);
  BuildBlock(test_world, 1);
  Tap(test_world, Keycode::KEY_F5);
  BuildBlock(test_world, 2);
  ASSERT_EQ(Blocks(test_world), 3u);

  Tap(test_world, Keycode::KEY_F9);

  EXPECT_EQ(Blocks(test_world), 2u);
}

TEST(QuickSaveTest, SavesAndLoadsOutsideBuildModeToo) {
  Z13TestWorld test_world;
  z13::testing::EnterBuildMode(test_world);
  BuildBlock(test_world, 0);
  z13::testing::ToggleBuildMode(test_world);
  Tap(test_world, Keycode::KEY_F5);
  z13::testing::ToggleBuildMode(test_world);
  BuildBlock(test_world, 1);
  z13::testing::ToggleBuildMode(test_world);
  ASSERT_EQ(Blocks(test_world), 2u);

  Tap(test_world, Keycode::KEY_F9);

  EXPECT_EQ(Blocks(test_world), 1u);
  EXPECT_FALSE(test_world.Player().has<z13::building::BuildingTool>());
}

TEST(QuickSaveTest, F9WithoutAQuickSaveChangesNothing) {
  Z13TestWorld test_world;
  z13::testing::EnterBuildMode(test_world);
  BuildBlock(test_world, 0);
  ASSERT_FALSE(std::filesystem::exists(test_world.QuickSavePath()));

  Tap(test_world, Keycode::KEY_F9);

  EXPECT_EQ(Blocks(test_world), 1u);
}

TEST(QuickSaveTest, HoldingF5SavesOnlyOnce) {
  Z13TestWorld test_world;
  z13::testing::EnterBuildMode(test_world);
  BuildBlock(test_world, 0);
  test_world.EmitInput(z13::testing::KeyDown(Keycode::KEY_F5));
  test_world.World().progress(kTestDeltaTime);
  test_world.World().progress(kTestDeltaTime);
  ASSERT_TRUE(std::filesystem::exists(test_world.QuickSavePath()));
  std::filesystem::remove(test_world.QuickSavePath());

  test_world.World().progress(kTestDeltaTime);
  test_world.World().progress(kTestDeltaTime);

  EXPECT_FALSE(std::filesystem::exists(test_world.QuickSavePath()));
}

}  // namespace
}  // namespace z13::state
