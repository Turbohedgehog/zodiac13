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

#include <Eigen/Dense>

#include <z13/components/building.h>
#include <z13/components/gameplay.h>

#include <lib_core/math.h>

#include "../../z13_module/tests/support/z13_test_world.h"

// Exercises PhysicsWorld's player-vs-block collision resolution (see
// PhysicsSystem::OnPlayerMoved, physics_system.cpp) through the real flecs
// pipeline, the same way building_block_test.cpp exercises block placement.
namespace z13::bullet_module {
namespace {

Eigen::Matrix4f TranslatedIdentity(float x, float y, float z) {
  Eigen::Matrix4f transform = Eigen::Matrix4f::Identity();
  z13::math::SetTranslation(Eigen::Vector3f(x, y, z), transform);
  return transform;
}

void SpawnBlockAt(flecs::world& world, const Eigen::Matrix4f& transform) {
  world.entity().set(transform).add<z13::building::BasicBlock>();
}

TEST(PhysicsCollisionTest, PlayerIsPushedOutOfOverlappingBlock) {
  z13::testing::Z13TestWorld test_world;
  flecs::world& world = test_world.World();
  flecs::entity player = test_world.Player();

  const Eigen::Matrix4f block_transform = TranslatedIdentity(2.f, 0.f, 0.f);
  SpawnBlockAt(world, block_transform);

  // Well inside the block (block half-extent is kBlockSize/2 = 0.3), offset
  // toward +X so the push-out direction is unambiguous.
  player.set(TranslatedIdentity(2.2f, 0.f, 0.f));

  const auto& resolved = player.get<Eigen::Matrix4f>();
  const float distance = (z13::math::ExtractTranslation<float>(resolved) -
                           z13::math::ExtractTranslation<float>(block_transform))
                              .norm();
  EXPECT_GE(
      distance,
      z13::building::kBlockSize / 2.f + z13::gameplay::kPlayerColliderRadius - z13::testing::kTestEpsilon);
}

TEST(PhysicsCollisionTest, PlayerUntouchedWhenClearOfBlocks) {
  z13::testing::Z13TestWorld test_world;
  flecs::world& world = test_world.World();
  flecs::entity player = test_world.Player();

  SpawnBlockAt(world, TranslatedIdentity(2.f, 0.f, 0.f));

  const Eigen::Matrix4f far_transform = TranslatedIdentity(50.f, 0.f, 0.f);
  player.set(far_transform);

  EXPECT_TRUE(player.get<Eigen::Matrix4f>().isApprox(far_transform));
}

}  // namespace
}  // namespace z13::bullet_module
