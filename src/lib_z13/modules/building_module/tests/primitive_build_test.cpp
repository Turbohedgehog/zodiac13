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
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <ostream>
#include <string>
#include <vector>

#include <Eigen/Dense>

#include <lib_core/state/world_json_store.h>
#include <lib_core/state/world_serializer.h>

#include <net_module/in_memory_transport.h>

#include <z13/components/building.h>
#include <z13/components/gameplay.h>
#include <z13/components/station.h>
#include <primitives/palette.h>
#include <z13_tests/shipped_station.h>

#include "../../z13_module/tests/support/building_test_helpers.h"
#include "../../z13_module/tests/support/test_network.h"
#include "../../z13_module/tests/support/world_json_test_helpers.h"
#include "../../z13_module/tests/support/z13_test_world.h"
#include "support/station_builders.h"

// Every primitive of the shipped palette through the building tool, alone and over the network.
namespace z13::building {
namespace {

using z13::station::BlockBrush;
using z13::testing::AddBuilder;
using z13::testing::BlocksOfType;
using z13::testing::Facing;
using z13::testing::kConnectArg;
using z13::testing::kMaxNetTestTicks;
using z13::testing::kNetTestDeltaTime;
using z13::testing::kTestServerEndpoint;
using z13::testing::RunNetworkUntil;
using z13::testing::StationWorld;
using z13::testing::Tap;
using z13::testing::Z13TestWorld;
using Keycode = z13::fbs::input::Keycode;

// Off the cell boundary at y = 0, so the brush and the destroy ray agree on the row.
const Eigen::Vector3f kBuilderPosition {0.f, 0.1f, 1.25f};
constexpr uint32_t kBuilderId = 7;
// The slot keys, in palette order.
constexpr std::array kSlotKeys {Keycode::KEY_1, Keycode::KEY_2, Keycode::KEY_3, Keycode::KEY_4, Keycode::KEY_5,
                                Keycode::KEY_6, Keycode::KEY_7, Keycode::KEY_8, Keycode::KEY_9};
// NEXT_PRIMITIVE's default key (building.fbs).
constexpr Keycode kNextPrimitiveKey = Keycode::KEY_E;
constexpr uint64_t kSettleTicks = 30;

struct PrimitiveSlot {
  z13::building::primitives::Primitive primitive;
  size_t slot {};
};

void PrintTo(const PrimitiveSlot& slot, std::ostream* out) {
  *out << slot.primitive.name;
}

std::vector<PrimitiveSlot> ShippedPrimitives() {
  std::vector<PrimitiveSlot> slots;
  const auto palette = z13::testing::ShippedPalette();
  for (size_t i = 0; palette && i < palette->primitives.size(); ++i) {
    slots.push_back({.primitive = palette->primitives[i], .slot = i});
  }
  return slots;
}

std::string Checkpoint(Z13TestWorld& test_world) {
  const auto json = z13::flecs_tools::WorldJsonStore::Save(test_world.World());
  EXPECT_TRUE(json.has_value()) << (json ? "" : json.error());
  return z13::testing::WithNormalizedSimulationTick(json.value_or(""));
}

class PrimitiveBuildTest : public ::testing::TestWithParam<PrimitiveSlot> {
 protected:
  const z13::building::primitives::Primitive& Primitive() const { return GetParam().primitive; }
};

TEST_P(PrimitiveBuildTest, BuildsAndSurvivesASaveAndLoad) {
  Z13TestWorld station = StationWorld();
  const flecs::entity builder = AddBuilder(station, kBuilderId, Facing(kBuilderPosition, Eigen::Vector3f::UnitX()));
  const size_t shipped = BlocksOfType(station.World(), Primitive().id).size();
  builder.set(BlockBrush {.spec = {.type_id = Primitive().id, .size = Primitive().min_size}});
  builder.add<RequestBuildBlock>();
  station.Tick();
  ASSERT_EQ(BlocksOfType(station.World(), Primitive().id).size(), shipped + 1) << Primitive().name << " was not built";
  const auto snapshot = z13::flecs_tools::CaptureState(station.World());
  ASSERT_TRUE(snapshot.has_value()) << snapshot.error();

  Z13TestWorld loaded = StationWorld();
  ASSERT_TRUE(z13::flecs_tools::RestoreWorld(loaded.World(), *snapshot).has_value());
  loaded.Tick();

  const auto same = [](const z13::station::Block& a, const z13::station::Block& b) {
    return a.spec == b.spec && a.cell == b.cell;
  };
  EXPECT_TRUE(std::ranges::equal(
      BlocksOfType(loaded.World(), Primitive().id), BlocksOfType(station.World(), Primitive().id), same));
}

TEST_P(PrimitiveBuildTest, AClientsBuildReachesTheServerAndOtherClients) {
  const auto network = std::make_shared<z13::net::InMemoryNetwork>();
  Z13TestWorld server(z13::testing::WithServerArg(z13::testing::SiteArgs()), network);
  Z13TestWorld client_a({std::string(kConnectArg), std::string(kTestServerEndpoint)}, network);
  Z13TestWorld client_b({std::string(kConnectArg), std::string(kTestServerEndpoint)}, network);
  const auto all = [&](uint64_t ticks) {
    RunNetworkUntil(*network, {server, client_a, client_b}, kNetTestDeltaTime, ticks, [] { return false; });
  };
  ASSERT_TRUE(RunNetworkUntil(*network, {server, client_a, client_b}, kNetTestDeltaTime, kMaxNetTestTicks, [&] {
    return client_a.World().has<z13::gameplay::Gameplay>() && client_b.World().has<z13::gameplay::Gameplay>() &&
        client_a.World().count<z13::gameplay::Player>() == 3 && client_b.World().count<z13::gameplay::Player>() == 3;
  }));
  const auto before = BlocksOfType(server.World(), Primitive().id).size();

  const auto step = [&] { all(1); };
  Tap(client_a, Keycode::KEY_TAB, step);
  all(kSettleTicks);
  // Past the slot keys, stepped to from the last slot.
  const size_t key = std::min(GetParam().slot, kSlotKeys.size() - 1);
  Tap(client_a, kSlotKeys.at(key), step);
  for (size_t i = key; i < GetParam().slot; ++i) {
    Tap(client_a, kNextPrimitiveKey, step);
  }
  z13::testing::Click(client_a, Keycode::MOUSE_BUTTON_LEFT, step);
  all(kSettleTicks);

  EXPECT_EQ(BlocksOfType(server.World(), Primitive().id).size(), before + 1) << Primitive().name;
  EXPECT_EQ(Checkpoint(server), Checkpoint(client_a));
  EXPECT_EQ(Checkpoint(server), Checkpoint(client_b));
}

INSTANTIATE_TEST_SUITE_P(
    ShippedPalette, PrimitiveBuildTest, ::testing::ValuesIn(ShippedPrimitives()),
    [](const ::testing::TestParamInfo<PrimitiveSlot>& info) { return info.param.primitive.name; });

}  // namespace
}  // namespace z13::building
