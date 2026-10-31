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

#include <lib_core/state/state_markers.h>
#include <lib_core/utils/math.h>

#include <z13/components/block.h>
#include <z13/components/block_changes.h>
#include <z13/components/conduit.h>
#include <z13/components/door.h>
#include <z13/components/gameplay.h>
#include <z13/components/player.h>
#include <z13/components/utilities.h>
#include <z13_module/gameplay/camera_look.h>
#include <z13_settings/door_tuning.h>
#include <z13_settings/physics_tuning.h>

#include "../../building_module/tests/support/station_builders.h"
#include "../../z13_module/tests/support/building_test_helpers.h"
#include "../../z13_module/tests/support/z13_test_world.h"
#include "support/room_blocks.h"

namespace z13::station {
namespace {

using Keycode = z13::fbs::input::Keycode;
using z13::testing::StationWorld;
using z13::testing::TickOnly;
using z13::testing::Z13TestWorld;

constexpr Keycode kUseKey = Keycode::KEY_E;
const Eigen::Vector2f kBeforeTheDoor {3.f, 3.5f};
// Long enough to fall to the box's floor.
constexpr int kFallTicks = 120;
// Short, to keep the tests short.
constexpr uint32_t kOpenTicks = 10;
constexpr uint32_t kManualTicks = 100;
constexpr uint32_t kDelayTicks = 20;
// A wait for the power's first step and the networks to settle.
constexpr int kPowerTicks = 12;

Eigen::Matrix4f At(const Eigen::Vector3f& position) {
  Eigen::Matrix4f transform = Eigen::Matrix4f::Identity();
  z13::math::SetTranslation(position, transform);
  return transform;
}

void Advance(Z13TestWorld& world, int ticks) {
  z13::testing::Ticks(world, ticks);
}

DoorState StateOf(flecs::entity door) {
  return door.has<DoorState>() ? door.get<DoorState>() : DoorState {};
}

void TuneDoors(Z13TestWorld& world) {
  auto& tuning = world.World().get_mut<z13::DoorTuning>();
  tuning.open_ticks = kOpenTicks;
  tuning.manual_open_ticks = kManualTicks;
  tuning.close_delay_ticks = kDelayTicks;
}

// The sealed box split by a door partition, as the interaction tests have it; nothing powers the door.
class DoorSystemTest : public ::testing::Test {
 protected:
  void SetUp() override {
    world_.Tick();
    TuneDoors(world_);
    testing::AddBlocks(world_, testing::SealedBox());
    testing::AddBlocks(world_, testing::DoorPartition());
    world_.Tick();
    door_ = FindDoor();
  }

  flecs::entity FindDoor() {
    flecs::entity found;
    world_.World().query_builder<const Block>().build().each([&](flecs::entity block, const Block& placed) {
      if (placed.spec.type_id == testing::IdOf(testing::kDoor)) {
        found = block;
      }
    });
    return found;
  }

  void StandBeforeTheDoor() {
    const Eigen::Vector2f xy = (testing::kRoomCorner.head<2>().cast<float>() + kBeforeTheDoor) * kCellSize;
    const float floor_top = static_cast<float>(testing::kRoomCorner.z() + 1) * kCellSize;
    world_.Player().set(At({xy.x(), xy.y(), floor_top + z13::PhysicsTuning {}.eye_height}))
        .set(z13::gameplay::LookAngles {});
    Advance(world_, kFallTicks);
  }

  Z13TestWorld world_ = StationWorld();
  flecs::entity door_;
};

TEST_F(DoorSystemTest, ADoorBlockGetsAClosedAutomaticState) {
  ASSERT_TRUE(door_.is_valid());
  ASSERT_TRUE(door_.has<DoorState>());

  EXPECT_EQ(StateOf(door_), DoorState {});
  EXPECT_EQ(StateOf(door_).mode, DoorMode::kAuto);
  EXPECT_TRUE(StateOf(door_).leaf_blocks);
}

TEST_F(DoorSystemTest, ADoorWithoutPowerIgnoresABodyInItsSensor) {
  StandBeforeTheDoor();
  Advance(world_, kManualTicks);

  EXPECT_EQ(StateOf(door_).position, 0);
}

TEST_F(DoorSystemTest, UsingADoorWithoutPowerCranksItOpenSlowlyAndItStaysOpen) {
  StandBeforeTheDoor();

  z13::testing::Tap(world_, kUseKey, TickOnly(world_));
  Advance(world_, kOpenTicks);
  EXPECT_GT(StateOf(door_).position, 0);
  EXPECT_LT(StateOf(door_).position, kDoorOpen / 2)
      << "by hand it takes " << kManualTicks << " ticks, not " << kOpenTicks;
  EXPECT_TRUE(StateOf(door_).leaf_blocks);

  Advance(world_, kManualTicks);
  EXPECT_EQ(StateOf(door_).position, kDoorOpen);
  EXPECT_FALSE(StateOf(door_).leaf_blocks);

  Advance(world_, 2 * static_cast<int>(kDelayTicks));
  EXPECT_EQ(StateOf(door_).position, kDoorOpen) << "a door worked by hand stays as it is left";
}

TEST_F(DoorSystemTest, UsingAnOpenDoorAgainClosesIt) {
  StandBeforeTheDoor();
  z13::testing::Tap(world_, kUseKey, TickOnly(world_));
  Advance(world_, kManualTicks + 1);
  ASSERT_EQ(StateOf(door_).position, kDoorOpen);

  z13::testing::Tap(world_, kUseKey, TickOnly(world_));
  Advance(world_, kManualTicks + 1);

  EXPECT_EQ(StateOf(door_).position, 0);
  EXPECT_TRUE(StateOf(door_).leaf_blocks);
}

TEST_F(DoorSystemTest, ALockedOrWeldedDoorStaysShutWhenUsed) {
  StandBeforeTheDoor();
  for (const DoorMode mode : {DoorMode::kLocked, DoorMode::kWelded}) {
    door_.set(DoorState {.mode = mode});

    z13::testing::Tap(world_, kUseKey, TickOnly(world_));
    Advance(world_, kManualTicks + 1);

    EXPECT_EQ(StateOf(door_).position, 0);
  }
}

TEST_F(DoorSystemTest, OpeningADoorListsItsOpeningForTheRenderer) {
  StandBeforeTheDoor();
  EXPECT_TRUE(world_.World().get<DoorPortals>().open.empty());

  z13::testing::Tap(world_, kUseKey, TickOnly(world_));
  Advance(world_, 2);

  ASSERT_EQ(world_.World().get<DoorPortals>().open.size(), 1u);
  EXPECT_GT(world_.World().get<DoorPortals>().open.front().extent.x(), 0);
}

// A door of its own with a reactor beside it, joined by a one-cell cable: nothing else in the way.
class PoweredDoorTest : public ::testing::Test {
 protected:
  static constexpr int kRow = 100;
  // The door fills x 100-105, y 100, z 100-109; the player stands before it, well inside its sensor.
  static constexpr int kPlayerCellY = 95;

  void SetUp() override {
    world_.Tick();
    TuneDoors(world_);
    door_ = AddBlock("Door", {6, 1, 10}, {kRow, kRow, kRow});
    AddBlock("Reactor", {4, 4, 4}, {kRow, kRow + 2, kRow});
    world_.World().entity().add<z13::flecs_tools::StateEntity>().set(z13::conduit::Conduit {
        .kind = 1, .cell = {kRow + 1, kRow + 1, kRow}, .axis = z13::conduit::ConduitAxis::kY, .length = 1});
    Advance(world_, kPowerTicks);
  }

  flecs::entity AddBlock(std::string_view name, const Eigen::Vector3i& size, const Eigen::Vector3i& cell) {
    const Block block {.spec = {.type_id = testing::IdOf(name), .size = size}, .cell = cell};
    return testing::AddBlock(world_, block);
  }

  void Stand(int cell_y) {
    world_.Player().set(At(Eigen::Vector3f(kRow + 2.5f, static_cast<float>(cell_y) + 0.5f, kRow + 5.5f) * kCellSize));
  }

  Z13TestWorld world_ = StationWorld();
  flecs::entity door_;
};

TEST_F(PoweredDoorTest, TheDoorIsSuppliedByTheReactor) {
  ASSERT_TRUE(door_.has<z13::utility::UtilitySupply>());

  EXPECT_TRUE(z13::utility::IsSupplied(door_));
}

TEST_F(PoweredDoorTest, ABodyInTheSensorOpensItFastAndItClosesAfterTheDelay) {
  Stand(kPlayerCellY);
  Advance(world_, static_cast<int>(kOpenTicks) + 2);
  EXPECT_EQ(StateOf(door_).position, kDoorOpen);
  EXPECT_FALSE(StateOf(door_).leaf_blocks);

  Stand(kPlayerCellY - 40);
  Advance(world_, static_cast<int>(kDelayTicks) - 5);
  EXPECT_EQ(StateOf(door_).position, kDoorOpen) << "it waits out the delay";

  Advance(world_, static_cast<int>(kDelayTicks + kOpenTicks) + 5);
  EXPECT_EQ(StateOf(door_).position, 0);
  EXPECT_TRUE(StateOf(door_).leaf_blocks);
}

TEST_F(PoweredDoorTest, ALockedDoorStaysShutForABodyInItsSensor) {
  door_.set(DoorState {.mode = DoorMode::kLocked});
  Stand(kPlayerCellY);

  Advance(world_, static_cast<int>(kOpenTicks) + 2);

  EXPECT_EQ(StateOf(door_).position, 0);
}

TEST_F(PoweredDoorTest, ADoorSetToManualIgnoresTheSensor) {
  door_.set(DoorState {.mode = DoorMode::kManual});
  Stand(kPlayerCellY);

  Advance(world_, static_cast<int>(kOpenTicks) + 2);

  EXPECT_EQ(StateOf(door_).position, 0);
}

}  // namespace
}  // namespace z13::station
