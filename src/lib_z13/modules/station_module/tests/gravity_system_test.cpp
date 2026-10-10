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

#include <lib_core/state/world_state.h>
#include <lib_core/utils/math.h>

#include <z13/components/gameplay.h>
#include <z13/components/gravity.h>
#include <z13/components/station.h>
#include <z13_module/gameplay/camera_look.h>
#include <z13_settings/physics_tuning.h>

#include "../../building_module/tests/support/station_builders.h"
#include "../../z13_module/tests/support/building_test_helpers.h"
#include "../../z13_module/tests/support/z13_test_world.h"
#include "support/room_blocks.h"

namespace z13::station {
namespace {

using z13::gravity::Gravity;
using z13::testing::StationWorld;
using z13::testing::Z13TestWorld;

constexpr float kTick = 1.f / 60.f;
// Long enough to fall the box's height.
constexpr int kFallTicks = 120;
// Long enough to walk 1.5 m.
constexpr int kWalkTicks = 30;
constexpr float kStandTolerance = 1e-2f;
const z13::PhysicsTuning kTuning {};

float FloorTop() {
  return static_cast<float>(testing::kRoomCorner.z() + 1) * kCellSize;
}

// The middle of the sealed box, `height` meters above its floor.
Eigen::Vector3f InBox(float height) {
  const Eigen::Vector3f middle =
      (testing::kRoomCorner.cast<float>() + Eigen::Vector3f::Constant(1.f + (testing::kSide / 2.f))) * kCellSize;
  return {middle.x(), middle.y(), FloorTop() + height};
}

Eigen::Matrix4f At(const Eigen::Vector3f& position) {
  Eigen::Matrix4f transform = Eigen::Matrix4f::Identity();
  z13::math::SetTranslation(position, transform);
  return transform;
}

Eigen::Vector3f PositionOf(flecs::entity entity) {
  return z13::math::ExtractTranslation<float>(entity.get<Eigen::Matrix4f>());
}

// A station with the sealed box built.
class GravitySystemTest : public ::testing::Test {
 protected:
  void SetUp() override {
    world_.Tick(kTick);
    testing::AddBlocks(world_, testing::SealedBox());
    world_.Tick(kTick);
  }

  void Advance(int ticks) {
    for (int i = 0; i < ticks; ++i) {
      world_.Tick(kTick);
    }
  }

  // Holds `key` for `ticks`.
  void Walk(z13::fbs::input::Keycode key, int ticks) {
    world_.EmitInput(z13::testing::KeyDown(key));
    Advance(ticks);
    world_.EmitInput(z13::testing::KeyUp(key));
    world_.Tick(kTick);
  }

  // Stands the player on the box's floor at `cell`'s x and y, facing +X: forward walks
  // along +X, left along +Y.
  flecs::entity StandAt(const Eigen::Vector2f& cell) {
    const flecs::entity player = world_.Player();
    const Eigen::Vector2f xy = (testing::kRoomCorner.head<2>().cast<float>() + cell) * kCellSize;
    player.set(At({xy.x(), xy.y(), FloorTop() + kTuning.eye_height})).set(z13::gameplay::LookAngles {});
    Advance(kFallTicks);
    return player;
  }

  Z13TestWorld world_ = StationWorld();
};

TEST_F(GravitySystemTest, APlayerInARoomFallsToItsFloorAndStands) {
  const flecs::entity player = world_.Player();
  player.set(At(InBox(1.f)));

  Advance(kFallTicks);

  EXPECT_TRUE(player.get<Gravity>().Pulls());
  EXPECT_TRUE(player.get<z13::gameplay::PlayerMotion>().grounded);
  EXPECT_NEAR(PositionOf(player).z(), FloorTop() + kTuning.eye_height, kStandTolerance);
}

TEST_F(GravitySystemTest, APlayerInVacuumFloats) {
  const flecs::entity player = world_.Player();
  const Eigen::Vector3f outside = InBox(10.f);
  player.set(At(outside));

  Advance(kFallTicks);

  EXPECT_FALSE(player.get<Gravity>().Pulls());
  EXPECT_TRUE(PositionOf(player).isApprox(outside));
}

// Gravity belongs to the room: any body with Gravity gets it, not only players.
TEST_F(GravitySystemTest, AnyBodyGetsTheGravityOfItsRoom) {
  const flecs::entity inside = world_.World().entity().set(At(InBox(1.f))).set(Gravity {});
  const flecs::entity outside = world_.World().entity().set(At(InBox(10.f))).set(Gravity {});

  world_.Tick(kTick);

  EXPECT_TRUE(inside.get<Gravity>().acceleration.isApprox(Eigen::Vector3f(0.f, 0.f, -kTuning.gravity)));
  EXPECT_FALSE(outside.get<Gravity>().Pulls());
}

// The door's leaf has no collision until doors open (f/doors): the player walks into the
// next room and keeps its gravity.
TEST_F(GravitySystemTest, APlayerWalksThroughAClosedDoorKeepingGravity) {
  testing::AddBlocks(world_, testing::DoorPartition());
  world_.Tick(kTick);
  // In front of the middle of the door's opening, y cells 2..5 of the partition at x = 5.
  const flecs::entity player = StandAt({3.f, 4.f});
  const float beyond_x = (static_cast<float>(testing::kRoomCorner.x()) + 8.f) * kCellSize;

  Walk(z13::fbs::input::Keycode::KEY_W, kWalkTicks);

  EXPECT_GE(PositionOf(player).x(), beyond_x);
  EXPECT_TRUE(player.get<Gravity>().Pulls());
  EXPECT_NEAR(PositionOf(player).z(), FloorTop() + kTuning.eye_height, kStandTolerance);
}

// One cell high, below step_height.
TEST_F(GravitySystemTest, APlayerStepsUpOntoALowSlab) {
  testing::AddBlock(world_, testing::At(testing::kFloor, {testing::kSide, 4, 1}, {}, {1, 7, 1}));
  world_.Tick(kTick);
  const flecs::entity player = StandAt({5.f, 4.f});

  Walk(z13::fbs::input::Keycode::KEY_A, kWalkTicks);

  EXPECT_NEAR(PositionOf(player).z(), FloorTop() + kCellSize + kTuning.eye_height, kStandTolerance);
}

// One meter high: the head passes over it, but the walk stops instead of lifting the player
// on top.
TEST_F(GravitySystemTest, AWaistHighBlockStopsTheWalk) {
  constexpr int kWallY = 7;
  testing::AddBlock(world_, testing::At(testing::kWall, {testing::kSide, 1, 4}, {}, {1, kWallY, 1}));
  world_.Tick(kTick);
  const flecs::entity player = StandAt({5.f, 4.f});

  Walk(z13::fbs::input::Keycode::KEY_A, kWalkTicks);

  EXPECT_LT(PositionOf(player).y(), static_cast<float>(testing::kRoomCorner.y() + kWallY) * kCellSize);
  EXPECT_NEAR(PositionOf(player).z(), FloorTop() + kTuning.eye_height, kStandTolerance);
}

// A session's physics.gravity replaces the player's own without the rooms changing.
TEST_F(GravitySystemTest, RoomGravityFollowsTheGravitySetting) {
  constexpr float kLowGravity = 1.6f;
  const flecs::entity body = world_.World().entity().set(At(InBox(1.f))).set(Gravity {});
  world_.Tick(kTick);

  z13::PhysicsTuning tuning = world_.World().get<z13::PhysicsTuning>();
  tuning.gravity = kLowGravity;
  world_.World().set(tuning);
  world_.Tick(kTick);

  EXPECT_TRUE(body.get<Gravity>().acceleration.isApprox(Eigen::Vector3f(0.f, 0.f, -kLowGravity)));
}

}  // namespace
}  // namespace z13::station
