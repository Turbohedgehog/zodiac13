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
#include <memory>
#include <string>
#include <vector>

#include <Eigen/Core>

#include <lib_core/state/world_json_store.h>
#include <net_module/in_memory_transport.h>
#include <touch/touch_mapping.h>
#include <z13/components/building.h>
#include <z13/components/gameplay.h>
#include <z13/components/input_events.h>
#include <z13/components/inventory.h>
#include <z13/components/player.h>
#include <z13_module/gameplay/gameplay_entities.h>
#include <z13_settings/settings.h>

#include "../support/building_test_helpers.h"
#include "../support/test_network.h"
#include "../support/world_json_test_helpers.h"
#include "../support/z13_test_world.h"

// The hotbar through real input: the starter kit, the number keys, the wheel, the touch
// cells and the tool key (docs/android-plan.md, stage 4).
namespace z13::inventory {
namespace {

using Keycode = z13::fbs::input::Keycode;
using z13::testing::Click;
using z13::testing::Tap;
using z13::testing::Z13TestWorld;

const Eigen::Vector2f kScreen(1000.f, 500.f);
constexpr uint64_t kFinger = 1;

uint8_t ActiveCell(flecs::entity player) {
  return player.get<Hotbar>().active_cell;
}

std::vector<flecs::entity> ItemsOf(flecs::world world, uint32_t player_id) {
  std::vector<flecs::entity> items;
  world.each([&items, player_id](flecs::entity item, const Item&, const CarriedBy& carried) {
    if (carried.player_id == player_id) {
      items.push_back(item);
    }
  });
  return items;
}

template <typename EventT>
EventT Touch(const Eigen::Vector2f& position) {
  EventT event;
  event.finger = kFinger;
  event.position = position;
  event.screen_size = kScreen;
  return event;
}

void TouchCell(Z13TestWorld& test_world, size_t cell) {
  const Eigen::Vector2f center =
      z13::input::touch::CellRect(test_world.World().get<z13::TouchLayout>(), cell, kScreen).center();
  test_world.EmitInput(Touch<z13::input::TouchDownEvent>(center));
  test_world.Tick();
  test_world.EmitInput(Touch<z13::input::TouchUpEvent>(center));
  test_world.Tick();
}

TEST(HotbarTest, EveryPlayerSpawnsEmptyHandedWithTheBuildToolInTheFirstCell) {
  Z13TestWorld test_world;
  test_world.Tick();
  const flecs::entity player = test_world.Player();

  const auto items = ItemsOf(test_world.World(), player.get<z13::gameplay::Player>().id);
  ASSERT_EQ(items.size(), 1u);
  EXPECT_EQ(items.front().get<Item>().kind, ItemKind::kBuildTool);
  EXPECT_EQ(items.front().get<CarriedBy>().cell, kStarterToolCell);
  EXPECT_EQ(ActiveCell(player), kSpawnActiveCell);
  EXPECT_FALSE(player.has<ActiveItem>());
  EXPECT_FALSE(player.has<z13::building::BuildingTool>());
}

TEST(HotbarTest, TheKeysTheWheelAndTheTouchCellsPickTheSameCells) {
  Z13TestWorld test_world;
  const flecs::entity player = test_world.Player();

  Tap(test_world, Keycode::KEY_4, z13::testing::TickOnly(test_world));
  EXPECT_EQ(ActiveCell(player), 3);
  Click(test_world, Keycode::MOUSE_WHEEL_DOWN);
  EXPECT_EQ(ActiveCell(player), 4);
  Click(test_world, Keycode::MOUSE_WHEEL_UP);
  Click(test_world, Keycode::MOUSE_WHEEL_UP);
  EXPECT_EQ(ActiveCell(player), 2);
  TouchCell(test_world, 6);
  EXPECT_EQ(ActiveCell(player), 6);
}

TEST(HotbarTest, TheWheelWrapsAround) {
  Z13TestWorld test_world;
  const flecs::entity player = test_world.Player();

  Tap(test_world, Keycode::KEY_1, z13::testing::TickOnly(test_world));
  Click(test_world, Keycode::MOUSE_WHEEL_UP);
  EXPECT_EQ(ActiveCell(player), kHotbarCells - 1);
  Click(test_world, Keycode::MOUSE_WHEEL_DOWN);
  EXPECT_EQ(ActiveCell(player), 0);
}

TEST(HotbarTest, TheToolKeyTakesTheToolAndGoesBackToTheCellBefore) {
  Z13TestWorld test_world;
  const flecs::entity player = test_world.Player();
  Tap(test_world, Keycode::KEY_6, z13::testing::TickOnly(test_world));

  z13::testing::EnterBuildMode(test_world);
  EXPECT_EQ(ActiveCell(player), kStarterToolCell);
  EXPECT_TRUE(player.has<z13::building::BuildingTool>());

  z13::testing::ToggleBuildMode(test_world);
  EXPECT_EQ(ActiveCell(player), 5);
  EXPECT_FALSE(player.has<z13::building::BuildingTool>());
}

TEST(HotbarTest, TheNumberKeyOfTheToolsCellTakesItToo) {
  Z13TestWorld test_world;
  const flecs::entity player = test_world.Player();

  Tap(test_world, Keycode::KEY_1, z13::testing::TickOnly(test_world));

  EXPECT_TRUE(player.has<z13::building::BuildingTool>());
}

TEST(HotbarTest, ALeavingPlayersItemsGoWithThem) {
  Z13TestWorld test_world;
  constexpr uint32_t kGuestId = 7;
  const flecs::entity guest = z13::gameplay::SpawnPlayer(test_world.World(), kGuestId);
  test_world.Tick();
  ASSERT_EQ(ItemsOf(test_world.World(), kGuestId).size(), 1u);

  guest.destruct();
  test_world.Tick();

  EXPECT_TRUE(ItemsOf(test_world.World(), kGuestId).empty());
  EXPECT_EQ(ItemsOf(test_world.World(), test_world.Player().get<z13::gameplay::Player>().id).size(), 1u);
}

std::string Checkpoint(Z13TestWorld& test_world) {
  const auto json = z13::flecs_tools::WorldJsonStore::Save(test_world.World());
  EXPECT_TRUE(json.has_value()) << (json ? "" : json.error());
  return z13::testing::WithNormalizedSimulationTick(json.value_or(""));
}

TEST(HotbarTest, AClientsPicksReachTheServerAndOtherClients) {
  const auto network = std::make_shared<z13::net::InMemoryNetwork>();
  Z13TestWorld server({std::string(z13::testing::kServerArg)}, network);
  Z13TestWorld client_a({std::string(z13::testing::kConnectArg), std::string(z13::testing::kTestServerEndpoint)},
                        network);
  Z13TestWorld client_b({std::string(z13::testing::kConnectArg), std::string(z13::testing::kTestServerEndpoint)},
                        network);
  const auto all = [&](uint64_t ticks) {
    z13::testing::RunNetworkUntil(*network, {server, client_a, client_b}, z13::testing::kNetTestDeltaTime, ticks,
                                  [] { return false; });
  };
  ASSERT_TRUE(z13::testing::RunNetworkUntil(
      *network, {server, client_a, client_b}, z13::testing::kNetTestDeltaTime, z13::testing::kMaxNetTestTicks, [&] {
        return client_a.World().count<z13::gameplay::Player>() == 3 &&
               client_b.World().count<z13::gameplay::Player>() == 3;
      }));

  constexpr uint64_t kSettleTicks = 30;
  const auto step = [&] { all(1); };
  Tap(client_a, Keycode::KEY_5, step);
  z13::testing::Click(client_a, Keycode::MOUSE_WHEEL_DOWN, step);
  z13::testing::EnterBuildMode(client_a, step);
  all(kSettleTicks);

  const flecs::entity on_server = server.World().lookup(client_a.Player().name().c_str());
  ASSERT_TRUE(on_server.is_valid());
  EXPECT_EQ(ActiveCell(on_server), kStarterToolCell);
  EXPECT_EQ(on_server.get<Hotbar>().previous_cell, 5);
  EXPECT_TRUE(on_server.has<z13::building::BuildingTool>());
  EXPECT_EQ(Checkpoint(server), Checkpoint(client_a));
  EXPECT_EQ(Checkpoint(server), Checkpoint(client_b));
}

}  // namespace
}  // namespace z13::inventory
