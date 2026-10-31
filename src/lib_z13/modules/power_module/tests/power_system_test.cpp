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
#include <optional>
#include <string_view>
#include <vector>

#include <Eigen/Dense>

#include <lib_core/state/state_markers.h>
#include <power/port_names.h>
#include <primitives/palette.h>
#include <z13/components/block_changes.h>
#include <z13/components/conduit.h>
#include <z13/components/overlay_layers.h>
#include <z13/components/power.h>
#include <z13/components/switch.h>
#include <z13/components/utilities.h>
#include <z13_settings/power_tuning.h>

#include "../../building_module/tests/support/station_builders.h"
#include "../../z13_module/tests/support/z13_test_world.h"

// Power networks of the shipped reactor, battery, breaker and terminal.
namespace z13::power_module {
namespace {

using z13::testing::StationWorld;
using z13::testing::Z13TestWorld;
using z13::utility::BatteryCharge;
using z13::utility::UtilitySupply;
using z13::utility::power::kNetworkKey;

constexpr uint32_t kCableKind = 1;
constexpr int kRow = 100;
// Rows of other blocks, each touching the reactor's side or a cable from it.
constexpr int kBreakerRow = kRow + 2;
constexpr int kFarRow = kRow + 3;
// Enough ticks for a step to land whatever tick the edit was made on.
constexpr int kStepTicks = 7;
constexpr int64_t kReactorWatts = 5000;
constexpr int64_t kTerminalWatts = 200;

flecs::entity AddBlock(Z13TestWorld& world, std::string_view name, int x, int y = kRow) {
  const auto& primitive = world.World().get<z13::building::primitives::BlockPalette>().palette.Find(name)->get();
  const z13::station::Block block {.spec = {.type_id = primitive.id, .size = primitive.min_size}, .cell = {x, y, kRow}};
  return z13::station::SpawnBlock(world.World().entity().add<z13::flecs_tools::StateEntity>(), block);
}

flecs::entity AddCable(Z13TestWorld& world, int x, int length, int y = kRow) {
  return world.World().entity().add<z13::flecs_tools::StateEntity>().set(z13::conduit::Conduit {
      .kind = kCableKind, .cell = {x, y, kRow}, .axis = z13::conduit::ConduitAxis::kX, .length = length});
}

void SetOutput(Z13TestWorld& world, int64_t watts) {
  auto& palette = world.World().get_mut<z13::building::primitives::BlockPalette>().palette;
  const auto reactor = std::ranges::find(palette.primitives, "Reactor", &z13::building::primitives::Primitive::name);
  reactor->ports.front().values.front().value = watts;
}

bool Supplied(flecs::entity consumer) {
  return consumer.has<UtilitySupply>() && consumer.get<UtilitySupply>().satisfied;
}

// A reactor (cells 100-103), a cable to a terminal at 107.
class PowerSystemTest : public ::testing::Test {
 protected:
  void SetUp() override {
    world_.Tick();
    reactor_ = AddBlock(world_, "Reactor", 100);
    terminal_ = AddBlock(world_, "Terminal", 107);
  }

  void Run() { z13::testing::Ticks(world_, kStepTicks); }

  Z13TestWorld world_ = StationWorld();
  flecs::entity reactor_;
  flecs::entity terminal_;
};

TEST_F(PowerSystemTest, TheOverlayFillsItsLayerOnlyWhileItIsShown) {
  AddCable(world_, 104, 3);
  auto& layers = world_.World().get_mut<z13::station::OverlayLayers>();
  ASSERT_NE(layers.Find("power"), nullptr);
  Run();
  EXPECT_TRUE(layers.Find("power")->boxes.empty());

  layers.shown = "power";
  Run();
  const z13::station::OverlayLayer& layer = *layers.Find("power");
  // The cable, the reactor and the terminal.
  EXPECT_EQ(layer.boxes.size(), 3u);
  EXPECT_NE(layer.label.find("5000 W produced"), std::string::npos) << layer.label;

  layers.shown.reset();
  Run();
  EXPECT_TRUE(layers.Find("power")->boxes.empty());
}

TEST_F(PowerSystemTest, TheWorldCarriesThePowerModuleAndItsBlocks) {
  const auto modules = world_.World().get<z13::utility::UtilityModules>().Sorted();

  EXPECT_TRUE(std::ranges::contains(modules, "power", &z13::utility::UtilityModule::name));
  EXPECT_TRUE(world_.World().get<z13::building::primitives::BlockPalette>().palette.Find("Breaker").has_value());
}

TEST_F(PowerSystemTest, AConsumerOnACableFromAProducerIsSupplied) {
  AddCable(world_, 104, 3);
  Run();

  EXPECT_TRUE(Supplied(terminal_));
  EXPECT_EQ(terminal_.get<z13::utility::UtilityDemand>().amount, kTerminalWatts);
  EXPECT_EQ(terminal_.get<z13::utility::UtilityDemand>().network, kNetworkKey);
}

TEST_F(PowerSystemTest, AConsumerWithoutACableIsAskingAndNotSupplied) {
  Run();

  ASSERT_TRUE(terminal_.has<UtilitySupply>());
  EXPECT_FALSE(Supplied(terminal_));
}

TEST_F(PowerSystemTest, ABlockWithoutAPowerPortGetsNoSupply) {
  const flecs::entity wall = AddBlock(world_, "Wall", 120);
  Run();

  EXPECT_FALSE(wall.has<UtilitySupply>());
}

// A second terminal on a row of its own, cabled to the reactor like the first.
flecs::entity AddFarTerminal(Z13TestWorld& world) {
  AddCable(world, 104, 3, kFarRow);
  return AddBlock(world, "Terminal", 107, kFarRow);
}

TEST_F(PowerSystemTest, AShortageCutsTheConsumerLaterInCellOrder) {
  AddCable(world_, 104, 3);
  const flecs::entity far_terminal = AddFarTerminal(world_);
  SetOutput(world_, kTerminalWatts);
  Run();

  EXPECT_TRUE(Supplied(terminal_));
  EXPECT_FALSE(Supplied(far_terminal));
}

TEST_F(PowerSystemTest, TheOrderBlocksAreBuiltInDoesNotChangeWhoIsServed) {
  Z13TestWorld other = StationWorld();
  other.Tick();
  const flecs::entity other_far = AddFarTerminal(other);
  AddCable(other, 104, 3);
  const flecs::entity other_near = AddBlock(other, "Terminal", 107);
  AddBlock(other, "Reactor", 100);
  SetOutput(other, kTerminalWatts);
  z13::testing::Ticks(other, kStepTicks);

  EXPECT_TRUE(Supplied(other_near));
  EXPECT_FALSE(Supplied(other_far));
}

TEST_F(PowerSystemTest, ABreakerCarriesPowerOnlyWhileItIsOn) {
  AddCable(world_, 104, 3, kBreakerRow);
  const flecs::entity breaker = AddBlock(world_, "Breaker", 107, kBreakerRow);
  AddCable(world_, 108, 3, kBreakerRow);
  const flecs::entity beyond = AddBlock(world_, "Terminal", 111, kBreakerRow);
  Run();
  EXPECT_FALSE(Supplied(beyond)) << "a breaker nobody used is off";

  breaker.set(z13::station::SwitchState {.on = true});
  Run();
  EXPECT_TRUE(Supplied(beyond));

  breaker.set(z13::station::SwitchState {.on = false});
  Run();
  EXPECT_FALSE(Supplied(beyond));
}

TEST_F(PowerSystemTest, ABatteryChargesFromTheSurplusAndCoversTheConsumerWithoutTheReactor) {
  AddCable(world_, 104, 3);
  const flecs::entity battery = AddBlock(world_, "Battery", 107, kFarRow);
  AddCable(world_, 104, 3, kFarRow);
  Run();
  ASSERT_TRUE(battery.has<BatteryCharge>());
  const int64_t charged = battery.get<BatteryCharge>().watt_ticks;
  EXPECT_GT(charged, 0);

  SetOutput(world_, 0);
  Run();
  EXPECT_TRUE(Supplied(terminal_)) << "the battery covers it";
  EXPECT_LT(battery.get<BatteryCharge>().watt_ticks, charged);
}

}  // namespace
}  // namespace z13::power_module
