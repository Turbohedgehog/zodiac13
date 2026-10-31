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

#include <Eigen/Dense>

#include <lib_core/state/state_markers.h>
#include <networks/station_networks.h>
#include <primitives/palette.h>
#include <z13/components/block_changes.h>
#include <z13/components/conduit.h>
#include <z13/components/switch.h>

#include "../../building_module/tests/support/station_builders.h"
#include "../../z13_module/tests/support/z13_test_world.h"

// The networks of the conduit runs and the blocks with ports, rebuilt when either changes.
namespace z13::conduit {
namespace {

using z13::testing::StationWorld;
using z13::testing::Z13TestWorld;
using z13::utility::networks::StationNetworks;

constexpr uint32_t kCableKind = 1;
constexpr uint32_t kWireKind = 2;
constexpr int kRow = 100;
// The two floors are 2 cells wide, with a run of 3 cells between them.
constexpr int kFirstFloorX = 100;
constexpr int kRunX = 102;
constexpr int kRunLength = 3;
constexpr int kSecondFloorX = 105;

const StationNetworks& Networks(Z13TestWorld& world) {
  return world.World().get<StationNetworks>();
}

void GiveFloorsAPowerPort(Z13TestWorld& world) {
  auto& palette = world.World().get_mut<z13::building::primitives::BlockPalette>().palette;
  const auto floor = std::ranges::find(palette.primitives, z13::testing::kFloorId, &z13::building::primitives::Primitive::id);
  ASSERT_NE(floor, palette.primitives.end());
  floor->ports.push_back({.network = "power", .role = "consumer", .priority = "doors", .values = {{.name = "demand", .value = 10}}});
}

flecs::entity AddFloor(Z13TestWorld& world, int x) {
  const z13::station::Block floor {
      .spec = {.type_id = z13::testing::kFloorId, .size = {2, 2, 1}}, .cell = {x, kRow, kRow}};
  return z13::station::SpawnBlock(world.World().entity().add<z13::flecs_tools::StateEntity>(), floor);
}

flecs::entity AddRun(Z13TestWorld& world, uint32_t kind, int x, int length) {
  return world.World().entity().add<z13::flecs_tools::StateEntity>().set(
      Conduit {.kind = kind, .cell = {x, kRow, kRow}, .axis = ConduitAxis::kX, .length = length});
}

class NetworkSystemTest : public ::testing::Test {
 protected:
  void SetUp() override {
    world_.Tick();
    GiveFloorsAPowerPort(world_);
    first_ = AddFloor(world_, kFirstFloorX);
    second_ = AddFloor(world_, kSecondFloorX);
  }

  Z13TestWorld world_ = StationWorld();
  flecs::entity first_;
  flecs::entity second_;
};

TEST_F(NetworkSystemTest, ACableBetweenTwoPortsJoinsThemIntoOneNetwork) {
  AddRun(world_, kCableKind, kRunX, kRunLength);
  world_.Tick();

  const auto first = Networks(world_).networks.OfPort(first_.id());
  ASSERT_TRUE(first.has_value());
  EXPECT_EQ(first, Networks(world_).networks.OfPort(second_.id()));
  EXPECT_EQ(Networks(world_).networks.All()[*first].key, "power");
}

TEST_F(NetworkSystemTest, WithoutACableEachPortIsANetworkOfItsOwn) {
  world_.Tick();

  const auto first = Networks(world_).networks.OfPort(first_.id());
  const auto second = Networks(world_).networks.OfPort(second_.id());
  ASSERT_TRUE(first.has_value());
  ASSERT_TRUE(second.has_value());
  EXPECT_NE(first, second);
}

TEST_F(NetworkSystemTest, AWireOfAnotherNetworkDoesNotJoinThePowerPorts) {
  AddRun(world_, kWireKind, kRunX, kRunLength);
  world_.Tick();

  EXPECT_NE(Networks(world_).networks.OfPort(first_.id()), Networks(world_).networks.OfPort(second_.id()));
}

TEST_F(NetworkSystemTest, RemovingTheCableSplitsTheNetwork) {
  const flecs::entity run = AddRun(world_, kCableKind, kRunX, kRunLength);
  world_.Tick();
  run.destruct();
  world_.Tick();

  EXPECT_NE(Networks(world_).networks.OfPort(first_.id()), Networks(world_).networks.OfPort(second_.id()));
}

TEST_F(NetworkSystemTest, ABreakerJoinsTheCablesOnItsSidesOnlyWhileItIsOn) {
  // first_, a cable, the breaker, a cable, and a floor further along instead of second_.
  z13::station::DestroyBlock(second_, second_.get<z13::station::Block>());
  const flecs::entity far_floor = AddFloor(world_, 107);
  AddRun(world_, kCableKind, 102, 2);
  const auto& palette = world_.World().get<z13::building::primitives::BlockPalette>().palette;
  const auto& breaker_primitive = palette.Find("Breaker")->get();
  const flecs::entity breaker = z13::station::SpawnBlock(
      world_.World().entity().add<z13::flecs_tools::StateEntity>(),
      {.spec = {.type_id = breaker_primitive.id, .size = breaker_primitive.min_size}, .cell = {104, kRow, kRow}});
  AddRun(world_, kCableKind, 105, 2);
  world_.Tick();
  EXPECT_NE(Networks(world_).networks.OfPort(first_.id()), Networks(world_).networks.OfPort(far_floor.id()));

  breaker.set(z13::station::SwitchState {.on = true});
  world_.Tick();
  EXPECT_EQ(Networks(world_).networks.OfPort(first_.id()), Networks(world_).networks.OfPort(far_floor.id()));

  breaker.set(z13::station::SwitchState {.on = false});
  world_.Tick();
  EXPECT_NE(Networks(world_).networks.OfPort(first_.id()), Networks(world_).networks.OfPort(far_floor.id()));
}

TEST_F(NetworkSystemTest, DestroyingAPortBlockDropsItFromTheNetworks) {
  AddRun(world_, kCableKind, kRunX, kRunLength);
  world_.Tick();
  z13::station::DestroyBlock(second_, second_.get<z13::station::Block>());
  world_.Tick();

  EXPECT_TRUE(Networks(world_).networks.OfPort(first_.id()).has_value());
  EXPECT_FALSE(Networks(world_).networks.OfPort(second_.id()).has_value());
}

TEST_F(NetworkSystemTest, NetworksAreRebuiltOnlyWhenARunOrAPortBlockChanges) {
  AddRun(world_, kCableKind, kRunX, kRunLength);
  world_.Tick();
  const uint64_t generation = Networks(world_).generation;

  z13::testing::Ticks(world_, 3);
  EXPECT_EQ(Networks(world_).generation, generation);

  // A block without a port changes nothing the networks hold.
  z13::station::SpawnBlock(world_.World().entity().add<z13::flecs_tools::StateEntity>(),
                           {.spec = {.type_id = z13::testing::kWallId, .size = {1, 1, 4}}, .cell = {200, kRow, kRow}});
  world_.Tick();
  EXPECT_EQ(Networks(world_).generation, generation);

  AddRun(world_, kWireKind, kRunX, kRunLength);
  world_.Tick();
  EXPECT_GT(Networks(world_).generation, generation);
}

}  // namespace
}  // namespace z13::conduit
