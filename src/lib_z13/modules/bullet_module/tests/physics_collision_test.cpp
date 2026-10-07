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
#include <optional>

#include <Eigen/Dense>

#include <bullet_module/bullet_components.h>

#include "../src/physics_world.h"

#include <z13/components/building.h>
#include <z13/components/gameplay.h>
#include <z13/components/input.h>
#include <z13/components/station.h>

#include <lib_core/utils/math.h>
#include <z13_module/gameplay/gameplay_entities.h>
#include <z13_settings/physics_tuning.h>
#include <z13_tests/test_time.h>

#include "../../z13_module/tests/support/block_test_helpers.h"
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
// Well inside a block (half-extent is kCubeEdge/2 = 0.25), offset toward +X so
// the push-out direction is unambiguous.
constexpr float kInsideOffset = 0.2f;

Eigen::Matrix4f TranslatedIdentity(float x, float y, float z) {
  Eigen::Matrix4f transform = Eigen::Matrix4f::Identity();
  z13::math::SetTranslation(Eigen::Vector3f(x, y, z), transform);
  return transform;
}

flecs::entity SpawnBlockAt(flecs::world& world, const Eigen::Matrix4f& transform) {
  return world.entity().set(z13::testing::CubeAt(z13::math::ExtractTranslation<float>(transform)));
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
const float kTouchingX = kBlockX - z13::testing::kCubeEdge / 2.f - kPlayerRadius;
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
      z13::testing::kCubeEdge / 2.f + kPlayerRadius - z13::testing::kTestEpsilon);
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

TEST(PhysicsBodySyncTest, RemovingBlockReleasesBody) {
  z13::testing::Z13TestWorld test_world;
  const flecs::entity block = SpawnBlockAt(test_world.World(), TranslatedIdentity(kBlockX, 0.f, 0.f));
  test_world.Tick(kTestDeltaTime);
  ASSERT_TRUE(block.has<RigidBody>());

  block.remove<z13::station::Block>();
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

TEST(PhysicsBodySyncTest, ChangingBlockCellInPlaceMovesBody) {
  z13::testing::Z13TestWorld test_world;
  const flecs::entity block = SpawnBlockAt(test_world.World(), TranslatedIdentity(kBlockX, 0.f, 0.f));
  test_world.Tick(kTestDeltaTime);

  block.set(z13::testing::CubeAt({kFarX, 0.f, 0.f}));
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

TEST(PhysicsBodySyncTest, RecreatedPhysicsWorldGetsBodiesOfUnchangedBlocks) {
  z13::testing::Z13TestWorld test_world;
  SpawnBlockAt(test_world.World(), TranslatedIdentity(kBlockX, 0.f, 0.f));
  // The second tick syncs the table the block moved to when it got RigidBody.
  test_world.Tick(kTestDeltaTime);
  test_world.Tick(kTestDeltaTime);

  test_world.World().remove<PhysicsWorld>();
  test_world.Tick(kTestDeltaTime);

  EXPECT_EQ(test_world.World().get<PhysicsWorld>().BodyCount(), 1u);
  EXPECT_GT(SettlePlayerAt(test_world, kBlockX + kInsideOffset).x(), kBlockX + kInsideOffset);
}

TEST(PhysicsBodySyncTest, RecreatedPhysicsWorldGetsAllBodiesWhenABlockIsAddedTheSameFrame) {
  z13::testing::Z13TestWorld test_world;
  SpawnBlockAt(test_world.World(), TranslatedIdentity(kBlockX, 0.f, 0.f));
  test_world.Tick(kTestDeltaTime);
  test_world.Tick(kTestDeltaTime);

  test_world.World().remove<PhysicsWorld>();
  SpawnBlockAt(test_world.World(), TranslatedIdentity(kFarX, 0.f, 0.f));
  test_world.Tick(kTestDeltaTime);

  EXPECT_EQ(test_world.World().get<PhysicsWorld>().BodyCount(), 2u);
}

// Block bodies are static and asleep, so stepping skips their AABBs (see PhysicsWorld::State).
TEST(PhysicsBodySyncTest, BlockBodiesAreStaticAndAsleep) {
  z13::testing::Z13TestWorld test_world;
  SpawnBlockAt(test_world.World(), TranslatedIdentity(kBlockX, 0.f, 0.f));
  test_world.Tick(kTestDeltaTime);
  test_world.Tick(kTestDeltaTime);

  btDiscreteDynamicsWorld& dynamics_world = test_world.World().get_mut<PhysicsWorld>().DynamicsWorld();
  EXPECT_FALSE(dynamics_world.getForceUpdateAllAabbs());
  const btCollisionObjectArray& bodies = dynamics_world.getCollisionObjectArray();
  ASSERT_EQ(bodies.size(), 1);
  EXPECT_TRUE(bodies[0]->isStaticObject());
  EXPECT_FALSE(bodies[0]->isActive());
}

// The body follows the primitive's shape from the palette, not its bounding box: above
// the low end of a slope (rising along +X) there is room for the player.
TEST(PhysicsBodySyncTest, SlopeCollidesByItsShape) {
  constexpr uint32_t kSlopeId = 5;  // assets/station/palette.json
  const Eigen::Vector3f clear_of_the_slope {0.1f, 0.5f, 0.9f};
  z13::testing::Z13TestWorld test_world;
  flecs::entity player = test_world.Player();
  test_world.World().entity().set(z13::station::Block {.spec = {.type_id = kSlopeId, .size = {4, 4, 4}}});
  test_world.Tick(kTestDeltaTime);

  player.set(TranslatedIdentity(clear_of_the_slope.x(), clear_of_the_slope.y(), clear_of_the_slope.z()));
  test_world.Tick(kTestDeltaTime);

  EXPECT_TRUE(z13::math::ExtractTranslation<float>(player.get<Eigen::Matrix4f>()).isApprox(clear_of_the_slope));
}

// Station primitives from the palette, as the test station is built of them.
constexpr uint32_t kWallId = 2;  // assets/station/palette.json
constexpr uint32_t kDoorId = 3;
const Eigen::Vector3i kWallSize {8, 1, 8};
const Eigen::Vector3i kDoorSize {6, 1, 10};
// Both stand across y = 2 m, from x = 0 and the floor up.
const Eigen::Vector3i kAcrossCell {0, 8, 0};
constexpr float kBeforeY = 1.f;
constexpr float kBeyondY = 3.f;
// The middle of the door's opening, where its leaf hangs (doors are closed until they open).
constexpr float kOpeningCentreX = 0.75f;
constexpr float kChestZ = 1.f;

flecs::entity PlaceBlock(z13::testing::Z13TestWorld& test_world, uint32_t type_id, const Eigen::Vector3i& size,
                         const Eigen::Vector3i& cell) {
  flecs::entity block = test_world.World().entity().set(
      z13::station::Block {.spec = {.type_id = type_id, .size = size}, .cell = cell});
  test_world.Tick(kTestDeltaTime);
  return block;
}

Eigen::Vector3f WalkAcross(z13::testing::Z13TestWorld& test_world, float x) {
  test_world.Player().set(TranslatedIdentity(x, kBeforeY, kChestZ));
  test_world.Tick(kTestDeltaTime);
  return MovePlayerWithinFrame(test_world, Eigen::Vector3f(x, kBeyondY, kChestZ));
}

TEST(StationCollisionTest, AWallStopsThePlayer) {
  z13::testing::Z13TestWorld test_world;
  PlaceBlock(test_world, kWallId, kWallSize, kAcrossCell);

  const Eigen::Vector3f moved = WalkAcross(test_world, kOpeningCentreX);

  EXPECT_NEAR(moved.y(), kAcrossCell.y() * z13::station::kCellSize - kPlayerRadius, kSweepTolerance);
}

TEST(StationCollisionTest, AClosedDoorStopsThePlayer) {
  z13::testing::Z13TestWorld test_world;
  PlaceBlock(test_world, kDoorId, kDoorSize, kAcrossCell);

  const Eigen::Vector3f moved = WalkAcross(test_world, kOpeningCentreX);

  EXPECT_NEAR(moved.y(), kAcrossCell.y() * z13::station::kCellSize - kPlayerRadius, kSweepTolerance);
}

// No gravity: walking into a slope slides the player up its face and over its top.
TEST(StationCollisionTest, WalkingIntoASlopeClimbsIt) {
  constexpr uint32_t kSlopeId = 5;  // 1 m, rising along +X
  constexpr float kStep = 0.1f;
  constexpr int kSteps = 30;
  constexpr float kSlopeTop = 1.f;
  z13::testing::Z13TestWorld test_world;
  PlaceBlock(test_world, kSlopeId, {4, 4, 4}, Eigen::Vector3i::Zero());
  const Eigen::Vector3f start {-1.f, 0.5f, kPlayerRadius + 0.05f};
  test_world.Player().set(TranslatedIdentity(start.x(), start.y(), start.z()));
  test_world.Tick(kTestDeltaTime);

  Eigen::Vector3f position = start;
  for (int i = 0; i < kSteps; ++i) {
    position = MovePlayerWithinFrame(test_world, position + Eigen::Vector3f(kStep, 0.f, 0.f));
  }

  EXPECT_GT(position.x(), kSlopeTop) << "stuck at the slope's foot";
  EXPECT_GT(position.z(), kSlopeTop) << "went through the slope";
}

// Placing and destroying blocks through the real pipeline: the block, its body and
// what later systems see must all go away in the frame the block is destroyed.
class BlockDestroyTest : public ::testing::Test {
 protected:
  void SetUp() override {
    z13::testing::EnterBuildMode(test_world_);
    z13::testing::Click(test_world_, z13::fbs::input::Keycode::MOUSE_BUTTON_LEFT);
    ASSERT_EQ(test_world_.World().count<z13::station::Block>(), 1);
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
    EXPECT_EQ(BodyCount(), static_cast<size_t>(world.count<z13::station::Block>()));
  });

  EXPECT_EQ(world.count<z13::station::Block>(), 0);
}

TEST_F(BlockDestroyTest, LateSystemsSeeTheDestroyedBlockInTheSameFrame) {
  flecs::world& world = test_world_.World();
  std::optional<int> seen_by_late_system;
  world.system("Test::LateBlockReader").kind(flecs::OnStore).read<z13::station::Block>().run(
      [&seen_by_late_system](flecs::iter& it) {
        while (it.next()) {
          seen_by_late_system = it.world().count<z13::station::Block>();
        }
      });

  ClickCheckingEachFrame(test_world_, [&] {
    EXPECT_EQ(seen_by_late_system, world.count<z13::station::Block>());
  });

  EXPECT_EQ(world.count<z13::station::Block>(), 0);
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
  z13::testing::Z13TestWorld test_world(std::vector<std::string> {});
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
