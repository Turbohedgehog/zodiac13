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

#include "../src/physics_world.h"

#include <z13/components/building.h>
#include <z13/components/gameplay.h>
#include <z13/components/input.h>

#include <lib_core/utils/math.h>
#include <z13_module/gameplay/gameplay_entities.h>
#include <z13_settings/physics_tuning.h>
#include <z13_tests/test_time.h>

#include "../../z13_module/tests/support/building_test_helpers.h"
#include "../../z13_module/tests/support/z13_test_world.h"

// Exercises PhysicsWorld's player-vs-block collision resolution and the
// systems that keep block bodies in sync with block components (see
// physics_system.cpp) through the real flecs pipeline, the same way
// building_block_test.cpp exercises block placement.
namespace z13::bullet_module {
namespace {

using z13::testing::kTestDeltaTime;
constexpr float kBlockX = 2.f;
constexpr float kFarX = 10.f;
// Well inside a block (half-extent is kBlockSize/2 = 0.3), offset toward +X so
// the push-out direction is unambiguous.
constexpr float kInsideOffset = 0.2f;

Eigen::Matrix4f TranslatedIdentity(float x, float y, float z) {
  Eigen::Matrix4f transform = Eigen::Matrix4f::Identity();
  z13::math::SetTranslation(Eigen::Vector3f(x, y, z), transform);
  return transform;
}

flecs::entity SpawnBlockAt(flecs::world& world, const Eigen::Matrix4f& transform) {
  return world.entity().set(transform).add<z13::building::BasicBlock>();
}

// Puts the player at `x` and lets the collision system run for a frame.
Eigen::Vector3f SettlePlayerAt(z13::testing::Z13TestWorld& test_world, float x) {
  flecs::entity player = test_world.Player();
  player.set(TranslatedIdentity(x, 0.f, 0.f));
  test_world.Tick(kTestDeltaTime);
  return z13::math::ExtractTranslation<float>(player.get<Eigen::Matrix4f>());
}

const float kPlayerRadius = z13::PhysicsTuning {}.player_collider_radius;
// Closest the player's center gets to the block's -X face.
const float kTouchingX = kBlockX - z13::building::kBlockSize / 2.f - kPlayerRadius;
const float kApproachX = kTouchingX - 1.f;
constexpr float kPastBlockX = kBlockX + 2.f;
// Small enough that the step hits the face, not the edge.
constexpr float kSlideY = 0.2f;
// The sweep stops a skin short of a surface.
constexpr float kSweepTolerance = 1e-2f;

// Moves the player to `target` mid-frame, after the sweep origin is recorded, as movement input does.
Eigen::Vector3f MovePlayerWithinFrame(z13::testing::Z13TestWorld& test_world, const Eigen::Vector3f& target) {
  flecs::entity mover = test_world.World()
                            .system<Eigen::Matrix4f>()
                            .with<z13::gameplay::Player>()
                            .kind<z13::input::ApplyActionFramePhase>()
                            .each([target](Eigen::Matrix4f& transform) { z13::math::SetTranslation(target, transform); });
  test_world.Tick(kTestDeltaTime);
  mover.destruct();
  return z13::math::ExtractTranslation<float>(test_world.Player().get<Eigen::Matrix4f>());
}

TEST(PhysicsCollisionTest, LongStepStopsAtABlockInsteadOfPassingThrough) {
  z13::testing::Z13TestWorld test_world;
  SpawnBlockAt(test_world.World(), TranslatedIdentity(kBlockX, 0.f, 0.f));
  SettlePlayerAt(test_world, kApproachX);

  const Eigen::Vector3f moved = MovePlayerWithinFrame(test_world, Eigen::Vector3f(kPastBlockX, 0.f, 0.f));

  EXPECT_NEAR(moved.x(), kTouchingX, kSweepTolerance);
}

TEST(PhysicsCollisionTest, DiagonalStepSlidesAlongTheBlockFace) {
  z13::testing::Z13TestWorld test_world;
  SpawnBlockAt(test_world.World(), TranslatedIdentity(kBlockX, 0.f, 0.f));
  SettlePlayerAt(test_world, kApproachX);

  const Eigen::Vector3f moved = MovePlayerWithinFrame(test_world, Eigen::Vector3f(kPastBlockX, kSlideY, 0.f));

  EXPECT_NEAR(moved.x(), kTouchingX, kSweepTolerance);
  EXPECT_NEAR(moved.y(), kSlideY, kSweepTolerance);
}

// Less than any IsNear-style epsilon, so a tolerance would let the player sink into the block.
constexpr float kTinyStep = 5e-3f;

TEST(PhysicsCollisionTest, TinyStepIntoABlockIsStoppedToo) {
  z13::testing::Z13TestWorld test_world;
  SpawnBlockAt(test_world.World(), TranslatedIdentity(kBlockX, 0.f, 0.f));
  SettlePlayerAt(test_world, kTouchingX);

  const Eigen::Vector3f moved = MovePlayerWithinFrame(test_world, Eigen::Vector3f(kTouchingX + kTinyStep, 0.f, 0.f));

  EXPECT_LE(moved.x(), kTouchingX);
}

TEST(PhysicsCollisionTest, StepAwayFromATouchingBlockIsFree) {
  z13::testing::Z13TestWorld test_world;
  SpawnBlockAt(test_world.World(), TranslatedIdentity(kBlockX, 0.f, 0.f));
  SettlePlayerAt(test_world, kTouchingX);

  const Eigen::Vector3f moved = MovePlayerWithinFrame(test_world, Eigen::Vector3f(kApproachX, 0.f, 0.f));

  EXPECT_NEAR(moved.x(), kApproachX, kSweepTolerance);
}

TEST(PhysicsCollisionTest, PlayerIsPushedOutOfOverlappingBlock) {
  z13::testing::Z13TestWorld test_world;
  flecs::world& world = test_world.World();
  flecs::entity player = test_world.Player();

  const Eigen::Matrix4f block_transform = TranslatedIdentity(kBlockX, 0.f, 0.f);
  SpawnBlockAt(world, block_transform);

  player.set(TranslatedIdentity(kBlockX + kInsideOffset, 0.f, 0.f));
  z13::flecs_tools::TickWorld(world, kTestDeltaTime);

  const auto& resolved = player.get<Eigen::Matrix4f>();
  const float distance = (z13::math::ExtractTranslation<float>(resolved) -
                           z13::math::ExtractTranslation<float>(block_transform))
                              .norm();
  EXPECT_GE(
      distance,
      z13::building::kBlockSize / 2.f + kPlayerRadius - z13::testing::kTestEpsilon);
}

// SweepOrigin is set in the frame the player first appears, so its collision runs then too.
TEST(PhysicsCollisionTest, NewPlayerInsideABlockIsPushedOutOnItsFirstFrame) {
  z13::testing::Z13TestWorld test_world;
  flecs::world& world = test_world.World();
  SpawnBlockAt(world, TranslatedIdentity(kBlockX, 0.f, 0.f));
  constexpr uint32_t kNewPlayerId = 2;
  const flecs::entity newcomer = z13::gameplay::SpawnPlayer(world, kNewPlayerId);
  newcomer.set(TranslatedIdentity(kBlockX + kInsideOffset, 0.f, 0.f));

  test_world.Tick(kTestDeltaTime);

  EXPECT_GT(z13::math::ExtractTranslation<float>(newcomer.get<Eigen::Matrix4f>()).x(), kBlockX + kInsideOffset);
}

TEST(PhysicsCollisionTest, PlayerUntouchedWhenClearOfBlocks) {
  z13::testing::Z13TestWorld test_world;
  flecs::world& world = test_world.World();
  flecs::entity player = test_world.Player();

  SpawnBlockAt(world, TranslatedIdentity(kBlockX, 0.f, 0.f));

  const Eigen::Matrix4f far_transform = TranslatedIdentity(50.f, 0.f, 0.f);
  player.set(far_transform);
  z13::flecs_tools::TickWorld(world, kTestDeltaTime);

  EXPECT_TRUE(player.get<Eigen::Matrix4f>().isApprox(far_transform));
}

TEST(PhysicsBodySyncTest, BlockCreatedDirectlyGetsRigidBodyAndCollides) {
  z13::testing::Z13TestWorld test_world;
  const flecs::entity block = SpawnBlockAt(test_world.World(), TranslatedIdentity(kBlockX, 0.f, 0.f));

  const Eigen::Vector3f resolved = SettlePlayerAt(test_world, kBlockX + kInsideOffset);

  EXPECT_TRUE(block.has<RigidBody>());
  EXPECT_GT(resolved.x(), kBlockX + kInsideOffset);
}

TEST(PhysicsBodySyncTest, RemovingBasicBlockTagReleasesBody) {
  z13::testing::Z13TestWorld test_world;
  const flecs::entity block = SpawnBlockAt(test_world.World(), TranslatedIdentity(kBlockX, 0.f, 0.f));
  test_world.Tick(kTestDeltaTime);
  ASSERT_TRUE(block.has<RigidBody>());

  block.remove<z13::building::BasicBlock>();
  test_world.Tick(kTestDeltaTime);

  EXPECT_FALSE(block.has<RigidBody>());
  const Eigen::Vector3f resolved = SettlePlayerAt(test_world, kBlockX + kInsideOffset);
  EXPECT_NEAR(resolved.x(), kBlockX + kInsideOffset, z13::testing::kTestEpsilon);
}

TEST(PhysicsBodySyncTest, DestroyedBlockReleasesBody) {
  z13::testing::Z13TestWorld test_world;
  const flecs::entity block = SpawnBlockAt(test_world.World(), TranslatedIdentity(kBlockX, 0.f, 0.f));
  test_world.Tick(kTestDeltaTime);

  block.destruct();
  test_world.Tick(kTestDeltaTime);

  const Eigen::Vector3f resolved = SettlePlayerAt(test_world, kBlockX + kInsideOffset);
  EXPECT_NEAR(resolved.x(), kBlockX + kInsideOffset, z13::testing::kTestEpsilon);
}

TEST(PhysicsBodySyncTest, ChangingBlockTransformInPlaceMovesBody) {
  z13::testing::Z13TestWorld test_world;
  const flecs::entity block = SpawnBlockAt(test_world.World(), TranslatedIdentity(kBlockX, 0.f, 0.f));
  test_world.Tick(kTestDeltaTime);

  block.set(TranslatedIdentity(kFarX, 0.f, 0.f));
  test_world.Tick(kTestDeltaTime);

  const Eigen::Vector3f at_old_place = SettlePlayerAt(test_world, kBlockX + kInsideOffset);
  EXPECT_NEAR(at_old_place.x(), kBlockX + kInsideOffset, z13::testing::kTestEpsilon);

  const Eigen::Vector3f at_new_place = SettlePlayerAt(test_world, kFarX + kInsideOffset);
  EXPECT_GT(at_new_place.x(), kFarX + kInsideOffset);
}

TEST(PhysicsBodySyncTest, SyncIsIdempotentAcrossFrames) {
  z13::testing::Z13TestWorld test_world;
  SpawnBlockAt(test_world.World(), TranslatedIdentity(kBlockX, 0.f, 0.f));

  for (int i = 0; i < 3; ++i) {
    test_world.Tick(kTestDeltaTime);
  }

  EXPECT_EQ(test_world.World().count<RigidBody>(), 1);
  EXPECT_GT(SettlePlayerAt(test_world, kBlockX + kInsideOffset).x(), kBlockX + kInsideOffset);
}

// Placing and destroying blocks through the real pipeline: the block, its body and
// what later systems see must all go away in the frame the block is destroyed.
class BlockDestroyTest : public ::testing::Test {
 protected:
  void SetUp() override {
    z13::testing::EnterBuildMode(test_world_);
    z13::testing::Click(test_world_, z13::fbs::input::Keycode::MOUSE_BUTTON_LEFT);
    ASSERT_EQ(test_world_.World().count<z13::building::BasicBlock>(), 1);
    // The block sits at the brush; standing still, the destroy ray reaches it again.
  }

  size_t BodyCount() {
    return test_world_.World().get<PhysicsWorld>().BodyCount();
  }

  z13::testing::Z13TestWorld test_world_;
};

// Click() runs two frames and would hide a one-frame lag, so press and release are
// stepped separately and the state is compared after every frame.
template <typename Check>
void ClickCheckingEachFrame(z13::testing::Z13TestWorld& test_world, Check check) {
  test_world.EmitInput(z13::testing::MouseDown(z13::fbs::input::Keycode::MOUSE_BUTTON_RIGHT));
  test_world.Tick(kTestDeltaTime);
  check();
  test_world.EmitInput(z13::testing::MouseUp(z13::fbs::input::Keycode::MOUSE_BUTTON_RIGHT));
  test_world.Tick(kTestDeltaTime);
  check();
}

TEST_F(BlockDestroyTest, BodyIsReleasedInTheFrameTheBlockIsDestroyed) {
  ASSERT_EQ(BodyCount(), 1u);
  flecs::world& world = test_world_.World();

  ClickCheckingEachFrame(test_world_, [&] {
    EXPECT_EQ(BodyCount(), static_cast<size_t>(world.count<z13::building::BasicBlock>()));
  });

  EXPECT_EQ(world.count<z13::building::BasicBlock>(), 0);
}

TEST_F(BlockDestroyTest, LateSystemsSeeTheDestroyedBlockInTheSameFrame) {
  flecs::world& world = test_world_.World();
  std::optional<int> seen_by_late_system;
  world.system("Test::LateBlockReader").kind(flecs::OnStore).read<z13::building::BasicBlock>().run(
      [&seen_by_late_system](flecs::iter& it) {
        while (it.next()) {
          seen_by_late_system = it.world().count<z13::building::BasicBlock>();
        }
      });

  ClickCheckingEachFrame(test_world_, [&] {
    EXPECT_EQ(seen_by_late_system, world.count<z13::building::BasicBlock>());
  });

  EXPECT_EQ(world.count<z13::building::BasicBlock>(), 0);
}

TEST(PhysicsMainMenuTest, ExitToMainMenuDestroysThePhysicsWorld) {
  z13::testing::Z13TestWorld test_world;
  flecs::world& world = test_world.World();
  // Placed through the real build path, which tags the block as a state entity.
  z13::testing::EnterBuildMode(test_world);
  z13::testing::Click(test_world, z13::fbs::input::Keycode::MOUSE_BUTTON_LEFT);
  ASSERT_EQ(world.get<PhysicsWorld>().BodyCount(), 1u);

  test_world.ExitToMainMenu();
  z13::flecs_tools::TickWorld(world, kTestDeltaTime);

  EXPECT_FALSE(world.has<PhysicsWorld>());
}

TEST(PhysicsMainMenuTest, NoPhysicsWorldBeforeTheGameStarts) {
  z13::testing::Z13TestWorld test_world(false);
  flecs::world& world = test_world.World();
  z13::flecs_tools::TickWorld(world, kTestDeltaTime);
  EXPECT_FALSE(world.has<PhysicsWorld>());

  test_world.StartGame();
  // The main menu froze the simulation; unfreezing lands one frame after it closes.
  z13::flecs_tools::TickWorld(world, kTestDeltaTime);
  z13::flecs_tools::TickWorld(world, kTestDeltaTime);

  ASSERT_TRUE(world.has<PhysicsWorld>());
  EXPECT_EQ(world.get<PhysicsWorld>().BodyCount(), 0u);
}

}  // namespace
}  // namespace z13::bullet_module
