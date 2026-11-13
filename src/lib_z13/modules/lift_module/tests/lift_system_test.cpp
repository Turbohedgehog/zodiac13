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

#include <string_view>

#include <Eigen/Dense>

#include <lib_core/time/simulation_clock.h>
#include <lib_core/utils/math.h>

#include <z13/components/gameplay/gameplay.h>
#include <z13/components/gameplay/kinematic_body.h>
#include <z13/components/gameplay/player.h>
#include <z13/components/station/block.h>
#include <z13/components/station/lift.h>
#include <player/camera_look.h>
#include <z13_settings/physics_tuning.h>

#include "../../building_module/tests/support/station_builders.h"
#include "../../station_module/tests/support/room_blocks.h"
#include "../../z13_module/tests/support/building_test_helpers.h"
#include "../../z13_module/tests/support/z13_test_world.h"

namespace z13::station {
namespace {

using Keycode = z13::fbs::input::Keycode;
using z13::kinematic::KinematicBody;
using z13::testing::StationWorld;
using z13::testing::Z13TestWorld;

constexpr Keycode kUseKey = Keycode::KEY_E;
constexpr std::string_view kSwitch = "Switch";
const Eigen::Vector3i kSwitchSize {1, 1, 2};
// The test's switch stands where the interaction tests put theirs, ahead of a player on the sealed box's floor.
constexpr int kSwitchCellsAhead = 4;
const Eigen::Vector2f kBeforeTheSwitch {3.f, 5.5f};
// Long enough to fall to the box's floor.
constexpr int kFallTicks = 120;
constexpr uint32_t kTripTicks = 20;
const Eigen::Vector3f kLower {10.f, 10.f, 0.f};
const Eigen::Vector3f kUpper {10.f, 10.f, 6.f};
const z13::PhysicsTuning kTuning {};

Eigen::Matrix4f At(const Eigen::Vector3f& position) {
  Eigen::Matrix4f transform = Eigen::Matrix4f::Identity();
  z13::math::SetTranslation(position, transform);
  return transform;
}

class LiftSystemTest : public ::testing::Test {
 protected:
  void SetUp() override {
    world_.Tick();
    testing::AddBlocks(world_, testing::SealedBox());
    world_.Tick();
    const flecs::entity player = world_.Player();
    const Eigen::Vector2f xy = (testing::kRoomCorner.head<2>().cast<float>() + kBeforeTheSwitch) * kCellSize;
    const float floor_top = static_cast<float>(testing::kRoomCorner.z() + 1) * kCellSize;
    player.set(At({xy.x(), xy.y(), floor_top + kTuning.eye_height})).set(z13::gameplay::LookAngles {});
    Advance(kFallTicks);
    const Eigen::Vector3i eye = (z13::math::ExtractTranslation<float>(player.get<Eigen::Matrix4f>()) / kCellSize)
                                    .array().floor().cast<int>();
    button_cell_ = eye + Eigen::Vector3i(kSwitchCellsAhead, 0, -1);
    testing::AddBlock(world_, {.spec = {.type_id = testing::IdOf(kSwitch), .size = kSwitchSize}, .cell = button_cell_});
    world_.Tick();
    lift_ = world_.World().entity()
                .set(Lift {.lower = kLower, .upper = kUpper, .lower_button = button_cell_, .trip_ticks = kTripTicks})
                .set(KinematicBody {.from = kLower, .to = kLower});
  }

  void Advance(int ticks) {
    for (int i = 0; i < ticks; ++i) {
      world_.Tick();
    }
  }

  void PressTheButton() {
    world_.EmitInput(z13::testing::KeyDown(kUseKey));
    world_.Tick();
    world_.EmitInput(z13::testing::KeyUp(kUseKey));
    world_.Tick();
  }

  Z13TestWorld world_ = StationWorld();
  Eigen::Vector3i button_cell_ = Eigen::Vector3i::Zero();
  flecs::entity lift_;
};

TEST_F(LiftSystemTest, UsingTheButtonSendsTheCabinUpAndAgainSendsItDown) {
  PressTheButton();
  EXPECT_EQ(lift_.get<KinematicBody>().to, kUpper);
  Advance(kTripTicks + 2);

  PressTheButton();
  EXPECT_EQ(lift_.get<KinematicBody>().from, kUpper);
  EXPECT_EQ(lift_.get<KinematicBody>().to, kLower);
}

TEST_F(LiftSystemTest, AButtonUsedMidTripChangesNothing) {
  PressTheButton();
  const KinematicBody sent = lift_.get<KinematicBody>();
  ASSERT_EQ(sent.to, kUpper);

  PressTheButton();

  const KinematicBody& after = lift_.get<KinematicBody>();
  EXPECT_EQ(after.to, sent.to);
  EXPECT_EQ(after.start_tick, sent.start_tick);
}

}  // namespace
}  // namespace z13::station
