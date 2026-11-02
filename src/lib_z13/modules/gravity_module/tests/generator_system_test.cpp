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

#include <algorithm>
#include <string>

#include <Eigen/Dense>

#include <lib_core/utils/math.h>

#include <primitives/palette.h>
#include <z13/components/block.h>
#include <z13/components/gravity.h>
#include <z13/components/utilities.h>
#include <z13_settings/physics_tuning.h>

#include "../../building_module/tests/support/station_builders.h"
#include "../../station_module/tests/support/room_blocks.h"
#include "../../z13_module/tests/support/z13_test_world.h"
#include "../src/generator_system.h"

// The pull of rooms with and without gravity generators.
namespace z13::gravity_module {
namespace {

using z13::gravity::Gravity;
using z13::gravity::GravityCharge;
using z13::gravity::GravitySources;
using z13::station::kCellSize;
using z13::testing::StationWorld;
using z13::testing::Z13TestWorld;
namespace testing = z13::station::testing;

constexpr uint32_t kRampTicks = 100;
// Next to the generator's room and not beyond the wall that splits the box.
constexpr uint32_t kReachCells = 1;
// Float rounding in a pull that a share of the field scales, in m/s2.
constexpr float kAccelerationTolerance = 1e-3f;
// Long enough to be built, to rise to the full field and to be felt.
constexpr int kRiseTicks = static_cast<int>(kRampTicks) + 20;
constexpr int kHalfRampTicks = static_cast<int>(kRampTicks) / 2;
constexpr size_t kGeneratorsBuilt = 2;

const Eigen::Vector3i kLeftCell = testing::kRoomCorner + Eigen::Vector3i(3, 6, 3);
const Eigen::Vector3i kRightCell = testing::kRoomCorner + Eigen::Vector3i(8, 6, 3);
const Eigen::Vector3i kGeneratorCell = testing::kRoomCorner + Eigen::Vector3i(6, 4, 1);

Eigen::Matrix4f AtCenterOf(const Eigen::Vector3i& cell) {
  Eigen::Matrix4f transform = Eigen::Matrix4f::Identity();
  const Eigen::Vector3f center = (cell.cast<float>() + Eigen::Vector3f::Constant(0.5f)) * kCellSize;
  z13::math::SetTranslation(center, transform);
  return transform;
}

// The sealed box split by the door partition, a body in each half, and a short ramp.
class GeneratorSystemTest : public ::testing::Test {
 protected:
  void SetUp() override {
    world_.Tick();
    auto& tuning = world_.World().get_mut<z13::PhysicsTuning>();
    tuning.generator_radius_cells = kReachCells;
    tuning.generator_ramp_ticks = kRampTicks;
    testing::AddBlocks(world_, testing::SealedBox());
    testing::AddBlocks(world_, testing::DoorPartition());
    left_ = world_.World().entity().set(AtCenterOf(kLeftCell)).set(Gravity {});
    right_ = world_.World().entity().set(AtCenterOf(kRightCell)).set(Gravity {});
    z13::testing::Ticks(world_, 2);
  }

  flecs::entity AddGenerator() {
    return testing::AddBlock(world_, {.spec = {.type_id = testing::IdOf("GravityGenerator"), .size = {4, 4, 4}},
                                      .cell = kGeneratorCell});
  }

  void Advance(int ticks) { z13::testing::Ticks(world_, ticks); }

  float PullOf(flecs::entity body) const { return -body.get<Gravity>().acceleration.z(); }

  float Full() { return world_.World().get<z13::PhysicsTuning>().gravity; }

  Z13TestWorld world_ = StationWorld();
  flecs::entity left_;
  flecs::entity right_;
};

TEST_F(GeneratorSystemTest, WithoutGeneratorsEveryRoomPulls) {
  Advance(kRiseTicks);

  EXPECT_NEAR(PullOf(left_), Full(), kAccelerationTolerance);
  EXPECT_NEAR(PullOf(right_), Full(), kAccelerationTolerance);
}

TEST_F(GeneratorSystemTest, APoweredGeneratorPullsTheRoomsWithinReachAndLeavesTheOthersFloating) {
  AddGenerator();

  Advance(kRiseTicks);

  EXPECT_NEAR(PullOf(right_), Full(), kAccelerationTolerance);
  EXPECT_NEAR(PullOf(left_), 0.f, kAccelerationTolerance);
}

TEST_F(GeneratorSystemTest, AGeneratorReachesRoomsWithinItsRadius) {
  world_.World().get_mut<z13::PhysicsTuning>().generator_radius_cells = testing::kSide;
  AddGenerator();

  Advance(kRiseTicks);

  EXPECT_NEAR(PullOf(left_), Full(), kAccelerationTolerance);
  EXPECT_NEAR(PullOf(right_), Full(), kAccelerationTolerance);
}

TEST_F(GeneratorSystemTest, TheFieldRisesOverTheRampAndFallsWithTheLossOfPower) {
  const flecs::entity generator = AddGenerator();
  Advance(kHalfRampTicks);
  EXPECT_GT(PullOf(right_), 0.f);
  EXPECT_LT(PullOf(right_), Full() - kAccelerationTolerance);

  Advance(kRiseTicks);
  ASSERT_NEAR(PullOf(right_), Full(), kAccelerationTolerance);

  generator.set(z13::utility::UtilitySupply {.satisfied = false});
  Advance(kHalfRampTicks);
  EXPECT_GT(PullOf(right_), 0.f);
  EXPECT_LT(PullOf(right_), Full() - kAccelerationTolerance);

  Advance(kRiseTicks);
  EXPECT_NEAR(PullOf(right_), 0.f, kAccelerationTolerance);
}

TEST_F(GeneratorSystemTest, AnUnpoweredGeneratorPullsNothingAnywhere) {
  AddGenerator().set(z13::utility::UtilitySupply {.satisfied = false});

  Advance(kRiseTicks);

  EXPECT_NEAR(PullOf(left_), 0.f, kAccelerationTolerance);
  EXPECT_NEAR(PullOf(right_), 0.f, kAccelerationTolerance);
}

TEST_F(GeneratorSystemTest, TheStrongestGeneratorInReachSetsTheRoomsPull) {
  AddGenerator().set(z13::utility::UtilitySupply {.satisfied = false});
  testing::AddBlock(world_, {.spec = {.type_id = testing::IdOf("GravityGenerator"), .size = {4, 4, 4}},
                             .cell = kGeneratorCell + Eigen::Vector3i(0, 0, 4)});

  Advance(kRiseTicks);

  EXPECT_NEAR(PullOf(right_), Full(), kAccelerationTolerance);
  ASSERT_EQ(world_.World().get<GravitySources>().list.size(), kGeneratorsBuilt);
}

TEST_F(GeneratorSystemTest, TheWorldCarriesTheGravityModuleAndItsBlock) {
  const auto modules = world_.World().get<z13::utility::UtilityModules>().Sorted();

  EXPECT_TRUE(std::ranges::contains(modules, std::string(kModuleName), &z13::utility::UtilityModule::name));
  EXPECT_TRUE(world_.World().get<z13::building::primitives::BlockPalette>().palette.Find("GravityGenerator").has_value());
}

}  // namespace
}  // namespace z13::gravity_module
