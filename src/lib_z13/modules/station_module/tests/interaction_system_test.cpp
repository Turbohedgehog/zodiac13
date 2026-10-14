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
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <Eigen/Dense>

#include <lib_core/state/world_json_store.h>
#include <lib_core/utils/math.h>

#include <net_module/in_memory_transport.h>

#include <z13/components/block.h>
#include <z13/components/block_brush.h>
#include <z13/components/gameplay.h>
#include <z13/components/input_events.h>
#include <z13/components/interaction.h>
#include <z13/components/player.h>
#include <z13/components/switch.h>
#include <z13_module/gameplay/camera_look.h>
#include <z13_settings/physics_tuning.h>
#include <z13_settings/settings.h>

#include "../../building_module/tests/support/station_builders.h"
#include "../../z13_module/tests/support/building_test_helpers.h"
#include "../../z13_module/tests/support/test_network.h"
#include "../../z13_module/tests/support/world_json_test_helpers.h"
#include "../../z13_module/tests/support/z13_test_world.h"
#include "support/room_blocks.h"

namespace z13::station {
namespace {

using Keycode = z13::fbs::input::Keycode;
using Methods = fbs::station::InteractionMethods;
using z13::testing::StationWorld;
using z13::testing::TickOnly;
using z13::testing::Z13TestWorld;

// USE's default key (actions.fbs); NEXT_PRIMITIVE's while building (building.fbs).
constexpr Keycode kUseKey = Keycode::KEY_E;
constexpr std::string_view kSwitch = "Switch";
const Eigen::Vector3i kSwitchSize {1, 1, 2};
// Long enough to fall to the box's floor.
constexpr int kFallTicks = 120;
constexpr int kHeldTicks = 3;
constexpr int kSwitchCellsAhead = 4;
const Eigen::Vector2f kBeforeTheSwitch {3.f, 5.5f};
const Eigen::Vector2f kBeforeTheDoor {3.f, 3.5f};
constexpr uint64_t kSettleTicks = 30;
const z13::PhysicsTuning kTuning {};

Eigen::Matrix4f At(const Eigen::Vector3f& position) {
  Eigen::Matrix4f transform = Eigen::Matrix4f::Identity();
  z13::math::SetTranslation(position, transform);
  return transform;
}

Eigen::Vector3i EyeCell(flecs::entity player) {
  return (z13::math::ExtractTranslation<float>(player.get<Eigen::Matrix4f>()) / kCellSize)
      .array().floor().cast<int>();
}

std::vector<Interaction> Found(Z13TestWorld& world, Methods method) {
  std::vector<Interaction> found;
  std::ranges::copy_if(world.World().get<Interactions>().list, std::back_inserter(found),
                       [method](const Interaction& interaction) { return interaction.method == method; });
  return found;
}

bool IsOn(flecs::entity block) {
  return block.has<SwitchState>() && block.get<SwitchState>().on;
}

// The sealed box; the door tests split it at x = 5 with the door partition (door at y 1..6).
// Players stand with their heads (radius 1.6 cells) clear of every block.
class InteractionSystemTest : public ::testing::Test {
 protected:
  void SetUp() override {
    world_.Tick();
    testing::AddBlocks(world_, testing::SealedBox());
    world_.Tick();
  }

  void AddDoorPartition() {
    testing::AddBlocks(world_, testing::DoorPartition());
    world_.Tick();
  }

  void Advance(int ticks) {
    for (int i = 0; i < ticks; ++i) {
      world_.Tick();
    }
  }

  // Stands the player on the box's floor at `cell`'s x and y, facing +X.
  flecs::entity StandAt(const Eigen::Vector2f& cell) {
    const flecs::entity player = world_.Player();
    const Eigen::Vector2f xy = (testing::kRoomCorner.head<2>().cast<float>() + cell) * kCellSize;
    const float floor_top = static_cast<float>(testing::kRoomCorner.z() + 1) * kCellSize;
    player.set(At({xy.x(), xy.y(), floor_top + kTuning.eye_height})).set(z13::gameplay::LookAngles {});
    Advance(kFallTicks);
    return player;
  }

  flecs::entity AddSwitchAhead(flecs::entity player, int cells) {
    const Block block {.spec = {.type_id = testing::IdOf(kSwitch), .size = kSwitchSize},
                       .cell = EyeCell(player) + Eigen::Vector3i(cells, 0, -1)};
    const flecs::entity added = testing::AddBlock(world_, block);
    world_.Tick();
    return added;
  }

  Z13TestWorld world_ = StationWorld();
};

TEST_F(InteractionSystemTest, UsingASwitchTurnsItOnAndOff) {
  const flecs::entity player = StandAt(kBeforeTheSwitch);
  const flecs::entity switch_block = AddSwitchAhead(player, kSwitchCellsAhead);
  ASSERT_FALSE(IsOn(switch_block));

  world_.EmitInput(z13::testing::KeyDown(kUseKey));
  world_.Tick();
  const auto uses = Found(world_, Methods::Use);
  ASSERT_EQ(uses.size(), 1u);
  EXPECT_EQ(uses.front().actor, player.id());
  EXPECT_EQ(uses.front().target, switch_block.id());
  EXPECT_TRUE(IsOn(switch_block));
  world_.EmitInput(z13::testing::KeyUp(kUseKey));
  world_.Tick();

  z13::testing::Tap(world_, kUseKey, TickOnly(world_));
  EXPECT_FALSE(IsOn(switch_block));
}

TEST_F(InteractionSystemTest, WhileBuildingTheUseKeyPicksAPrimitiveInstead) {
  const flecs::entity switch_block = AddSwitchAhead(StandAt(kBeforeTheSwitch), kSwitchCellsAhead);
  z13::testing::EnterBuildMode(world_);
  Advance(2);

  z13::testing::Tap(world_, kUseKey, TickOnly(world_));
  EXPECT_FALSE(IsOn(switch_block));
}

// Holding use is how a door without power is cranked open (docs/station-systems-plan.md, "Двери").
TEST_F(InteractionSystemTest, HoldingUseOnADoorInteractsEveryTickItIsHeld) {
  AddDoorPartition();
  StandAt(kBeforeTheDoor);

  world_.EmitInput(z13::testing::KeyDown(kUseKey));
  for (int tick = 0; tick < kHeldTicks; ++tick) {
    world_.Tick();
    EXPECT_EQ(Found(world_, Methods::Hold).size(), 1u) << "tick " << tick;
    EXPECT_TRUE(Found(world_, Methods::Use).empty()) << "a door takes no single use";
  }
  world_.EmitInput(z13::testing::KeyUp(kUseKey));
  world_.Tick();
  EXPECT_TRUE(Found(world_, Methods::Hold).empty());
}

TEST_F(InteractionSystemTest, PresenceLastsWhileThePlayerIsInTheDoorsSensor) {
  AddDoorPartition();
  const flecs::entity player = StandAt(kBeforeTheDoor);
  const auto near = Found(world_, Methods::Presence);
  ASSERT_EQ(near.size(), 1u);
  EXPECT_EQ(near.front().actor, player.id());

  const Eigen::Vector3f far_away =
      (testing::kRoomCorner + Eigen::Vector3i(testing::kSide * 4, 0, 0)).cast<float>() * kCellSize;
  player.set(At(far_away));
  world_.Tick();
  EXPECT_TRUE(Found(world_, Methods::Presence).empty());
}

std::string Checkpoint(Z13TestWorld& test_world) {
  const auto json = z13::flecs_tools::WorldJsonStore::Save(test_world.World());
  EXPECT_TRUE(json.has_value()) << (json ? "" : json.error());
  return z13::testing::WithNormalizedSimulationTick(json.value_or(""));
}

size_t SlotOf(std::string_view primitive) {
  const auto& primitives = z13::testing::ShippedPalette()->primitives;
  return static_cast<size_t>(std::ranges::distance(
      primitives.begin(), std::ranges::find(primitives, primitive, &z13::building::primitives::Primitive::name)));
}

std::vector<bool> SwitchesOn(Z13TestWorld& test_world) {
  std::vector<bool> on;
  test_world.World().query_builder<const Block>().build().each([&](flecs::entity block, const Block& spec) {
    if (spec.spec.type_id == testing::IdOf(kSwitch)) {
      on.push_back(IsOn(block));
    }
  });
  return on;
}

// The client builds a switch where its brush points, 5 m ahead, and uses it from there.
TEST(InteractionNetTest, AClientsUseTurnsTheSwitchOnForEveryone) {
  constexpr float kPastTheBrush = 6.f;
  constexpr int kLookAsideDelta = 7;
  const auto network = std::make_shared<z13::net::InMemoryNetwork>();
  z13::Settings settings = z13::MakeSettings();
  settings.building->use_reach_distance = kPastTheBrush;
  Z13TestWorld server(z13::testing::WithServerArg(z13::testing::SiteArgs()), network, settings);
  Z13TestWorld client(
      {std::string(z13::testing::kConnectArg), std::string(z13::testing::kTestServerEndpoint)}, network);
  const auto all = [&](uint64_t ticks) {
    z13::testing::RunNetworkUntil(
        *network, {server, client}, z13::testing::kNetTestDeltaTime, ticks, [] { return false; });
  };
  ASSERT_TRUE(z13::testing::RunNetworkUntil(
      *network, {server, client}, z13::testing::kNetTestDeltaTime, z13::testing::kMaxNetTestTicks, [&] {
        return client.World().has<z13::gameplay::Gameplay>() && client.World().count<z13::gameplay::Player>() == 2;
      }));
  const auto step = [&] { all(1); };
  all(kFallTicks);
  // Turned a little: facing straight along +X from the spawn, the ray would graze a cell boundary
  // and miss a switch one cell wide.
  z13::input::MouseMoveEvent look_aside;
  look_aside.delta = {.x = kLookAsideDelta, .y = 0};
  client.EmitInput(look_aside);
  all(kSettleTicks);

  z13::testing::Tap(client, Keycode::KEY_TAB, step);
  all(kSettleTicks);
  client.World().set(PaletteChoice {.slot = SlotOf(kSwitch)});
  all(kSettleTicks);
  z13::testing::Click(client, Keycode::MOUSE_BUTTON_LEFT, step);
  all(kSettleTicks);
  z13::testing::Tap(client, Keycode::KEY_TAB, step);
  all(kSettleTicks);
  ASSERT_EQ(SwitchesOn(server), std::vector {false});

  z13::testing::Tap(client, kUseKey, step);
  all(kSettleTicks);
  EXPECT_EQ(SwitchesOn(server), std::vector {true});
  EXPECT_EQ(SwitchesOn(client), std::vector {true});
  EXPECT_EQ(Checkpoint(server), Checkpoint(client));
}

}  // namespace
}  // namespace z13::station
