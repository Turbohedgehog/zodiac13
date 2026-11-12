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

#include <bullet_module/bullet_components.h>

#include "../src/physics_world.h"

#include <lib_core/utils/math.h>
#include <z13/components/gameplay/kinematic_body.h>
#include <z13/components/gameplay/player_parts.h>
#include <z13/components/utility/gravity.h>
#include <z13_settings/physics_tuning.h>

#include "../../z13_module/tests/support/z13_test_world.h"

namespace z13::bullet_module {
namespace {

using z13::kinematic::KinematicBody;

// A real frame: the test clock's one-second frames drop a player 9.8 m a tick, so he is never grounded at a tick's start.
constexpr float kFrameTime = 1.f / 60.f;
constexpr uint32_t kSlabPrimitiveId = 2;  // the wall's box (assets/station/palette.json)
const Eigen::Vector3i kSlabSize {16, 16, 2};
constexpr float kSlabTop = 2 * z13::station::kCellSize;
constexpr float kRise = 3.f;
constexpr uint64_t kStartTick = 5;
constexpr uint32_t kDuration = 40;
constexpr int kTicksToArrive = 60;
const Eigen::Vector3f kSlabCorner {-2.f, -2.f, 0.f};
const Eigen::Vector3f kGravity {0.f, 0.f, -9.8f};
// The sweep leaves the player a skin above the floor.
constexpr float kStandTolerance = 5e-2f;
const float kEyeHeight = z13::PhysicsTuning {}.eye_height;

KinematicBody SlabMovingBy(const Eigen::Vector3f& travel) {
  return {.spec = {.type_id = kSlabPrimitiveId, .size = kSlabSize},
          .from = kSlabCorner,
          .to = kSlabCorner + travel,
          .start_tick = kStartTick,
          .duration_ticks = kDuration};
}

KinematicBody RisingSlab() {
  return SlabMovingBy(Eigen::Vector3f(0.f, 0.f, kRise));
}

// A player grounded on the slab at its centre, under gravity.
flecs::entity StandOnTheSlab(z13::testing::Z13TestWorld& test_world) {
  flecs::entity player = test_world.Player();
  player.set(z13::gravity::Gravity {.acceleration = kGravity});
  player.set(z13::gameplay::PlayerMotion {.grounded = true});
  Eigen::Matrix4f transform = Eigen::Matrix4f::Identity();
  z13::math::SetTranslation(Eigen::Vector3f(0.f, 0.f, kSlabTop + kEyeHeight), transform);
  player.set(transform);
  return player;
}

Eigen::Vector3f PositionOf(flecs::entity entity) {
  return z13::math::ExtractTranslation<float>(entity.get<Eigen::Matrix4f>());
}

TEST(KinematicBodyTest, ThePositionIsAFunctionOfTheTickAndStaysAtTheEnds) {
  const KinematicBody body = RisingSlab();

  EXPECT_EQ(z13::kinematic::PositionAt(body, 0), body.from);
  EXPECT_EQ(z13::kinematic::PositionAt(body, kStartTick), body.from);
  EXPECT_NEAR(z13::kinematic::PositionAt(body, kStartTick + kDuration / 2).z(), kRise / 2.f, 1e-5f);
  EXPECT_EQ(z13::kinematic::PositionAt(body, kStartTick + kDuration), body.to);
  EXPECT_EQ(z13::kinematic::PositionAt(body, kStartTick + kDuration + 100), body.to);
}

TEST(KinematicBodyTest, ARisingSlabCarriesAPlayerStandingOnIt) {
  z13::testing::Z13TestWorld test_world;
  test_world.World().entity().set(RisingSlab());
  flecs::entity player = StandOnTheSlab(test_world);

  for (int i = 0; i < kTicksToArrive; ++i) {
    test_world.Tick(kFrameTime);
  }

  EXPECT_NEAR(PositionOf(player).z(), kSlabTop + kRise + kEyeHeight, kStandTolerance);
  EXPECT_TRUE(player.get<z13::gameplay::PlayerMotion>().grounded);
}

TEST(KinematicBodyTest, ASlabMovingSidewaysTakesAPlayerStandingOnItAlong) {
  z13::testing::Z13TestWorld test_world;
  test_world.World().entity().set(SlabMovingBy(Eigen::Vector3f(kRise, 0.f, 0.f)));
  flecs::entity player = StandOnTheSlab(test_world);

  for (int i = 0; i < kTicksToArrive; ++i) {
    test_world.Tick(kFrameTime);
  }

  const Eigen::Vector3f position = PositionOf(player);
  EXPECT_NEAR(position.x(), kRise, kStandTolerance);
  EXPECT_NEAR(position.z(), kSlabTop + kEyeHeight, kStandTolerance);
}

TEST(KinematicBodyTest, ASlabThatIsGoneLeavesNoBody) {
  z13::testing::Z13TestWorld test_world;
  flecs::entity slab = test_world.World().entity().set(RisingSlab());
  test_world.Tick(kFrameTime);
  const size_t with_slab = test_world.World().get<PhysicsWorld>().BodyCount();
  ASSERT_GT(with_slab, 0U);

  slab.destruct();
  test_world.Tick(kFrameTime);

  EXPECT_EQ(test_world.World().get<PhysicsWorld>().BodyCount(), with_slab - 1);
}

}  // namespace
}  // namespace z13::bullet_module
