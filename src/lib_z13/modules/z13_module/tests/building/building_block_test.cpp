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

#include <Eigen/Dense>

#include <bullet_module/bullet_components.h>

#include <z13/components/building.h>
#include <z13/components/gameplay.h>
#include <z13/components/input.h>
#include <z13_module/gameplay/camera_look.h>

#include "../support/building_test_helpers.h"
#include "../support/z13_test_world.h"

// Exercises the real build/destroy pipeline end to end (mouse click -> flecs
// input events -> BuildingInputSystem -> BuildingSystem -> bullet_module)
// through an actual flecs::world, the same way input_pipeline_test.cpp
// exercises movement.
namespace z13::building {
namespace {

using z13::testing::Click;
using z13::testing::EnterBuildMode;
using z13::testing::kTestDeltaTime;
using z13::testing::KeyDown;

std::optional<Eigen::Matrix4f> BrushTransform(flecs::world& world) {
  std::optional<Eigen::Matrix4f> transform;
  world.query_builder<const Brush, const Eigen::Matrix4f>()
      .build()
      .each([&](const Brush&, const Eigen::Matrix4f& brush_transform) {
        transform = brush_transform;
      });
  return transform;
}

TEST(BuildingBlockTest, BuildBlockDoesNothingOutsideBuildMode) {
  z13::testing::Z13TestWorld test_world;

  Click(test_world, z13::fbs::input::Keycode::MOUSE_BUTTON_LEFT);

  EXPECT_EQ(test_world.World().count<BasicBlock>(), 0);
}

TEST(BuildingBlockTest, BuildBlockSpawnsBlockAtBrushPosition) {
  z13::testing::Z13TestWorld test_world;
  EnterBuildMode(test_world);

  const auto brush_transform = BrushTransform(test_world.World());
  ASSERT_TRUE(brush_transform.has_value());

  Click(test_world, z13::fbs::input::Keycode::MOUSE_BUTTON_LEFT);

  ASSERT_EQ(test_world.World().count<BasicBlock>(), 1);

  test_world.World().query_builder<const BasicBlock, const Eigen::Matrix4f>()
      .build()
      .each([&](const BasicBlock&, const Eigen::Matrix4f& block_transform) {
        EXPECT_TRUE(block_transform.isApprox(*brush_transform));
      });
}

TEST(BuildingBlockTest, BuildBlockCreatesRigidBody) {
  z13::testing::Z13TestWorld test_world;
  EnterBuildMode(test_world);
  Click(test_world, z13::fbs::input::Keycode::MOUSE_BUTTON_LEFT);

  bool found_rigid_body = false;
  test_world.World().query_builder<const BasicBlock>()
      .build()
      .each([&](flecs::entity block, const BasicBlock&) {
        found_rigid_body = block.has<z13::bullet_module::RigidBody>();
      });

  EXPECT_TRUE(found_rigid_body);
}

// DestroyBlock now raycasts along the player's forward axis (see
// PhysicsSystem::ProcessDestroyBlockRequest) instead of box-testing the
// brush; the default player faces the same +X axis it built the block on.
TEST(BuildingBlockTest, DestroyBlockRemovesLookedAtBlock) {
  z13::testing::Z13TestWorld test_world;
  EnterBuildMode(test_world);
  Click(test_world, z13::fbs::input::Keycode::MOUSE_BUTTON_LEFT);
  ASSERT_EQ(test_world.World().count<BasicBlock>(), 1);

  Click(test_world, z13::fbs::input::Keycode::MOUSE_BUTTON_RIGHT);

  EXPECT_EQ(test_world.World().count<BasicBlock>(), 0);
}

TEST(BuildingBlockTest, DestroyBlockDoesNothingWhenNoBlockInSight) {
  z13::testing::Z13TestWorld test_world;
  EnterBuildMode(test_world);
  Click(test_world, z13::fbs::input::Keycode::MOUSE_BUTTON_LEFT);
  ASSERT_EQ(test_world.World().count<BasicBlock>(), 1);

  // Move the player well past the block (still facing the same +X axis it
  // was placed on), so the destroy raycast no longer reaches it.
  test_world.EmitInput(KeyDown(z13::fbs::input::Keycode::KEY_W));
  test_world.World().progress(kTestDeltaTime);
  test_world.World().progress(kTestDeltaTime);

  Click(test_world, z13::fbs::input::Keycode::MOUSE_BUTTON_RIGHT);

  EXPECT_EQ(test_world.World().count<BasicBlock>(), 1);
}

TEST(BuildingBlockTest, DestroyBlockOnlyRemovesOneOverlappingBlock) {
  z13::testing::Z13TestWorld test_world;
  EnterBuildMode(test_world);

  Click(test_world, z13::fbs::input::Keycode::MOUSE_BUTTON_LEFT);
  Click(test_world, z13::fbs::input::Keycode::MOUSE_BUTTON_LEFT);
  ASSERT_EQ(test_world.World().count<BasicBlock>(), 2);

  Click(test_world, z13::fbs::input::Keycode::MOUSE_BUTTON_RIGHT);

  EXPECT_EQ(test_world.World().count<BasicBlock>(), 1);
}

}  // namespace
}  // namespace z13::building
