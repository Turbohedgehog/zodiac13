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
#include <deque>
#include <format>
#include <memory>
#include <numeric>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <lib_core/utils/math.h>

#include <net_module/in_memory_transport.h>

#include <z13/components/gameplay.h>
#include <z13/components/input.h>
#include <z13/components/net.h>
#include <z13/components/player_action.h>
#include <z13_module/gameplay/gameplay_entities.h>
#include <z13_module/input/action_negotiation.h>
#include <z13_module/input/input_config_loader.h>

#include <actions_generated.h>

#include "../support/building_test_helpers.h"
#include "../support/test_network.h"
#include "../support/z13_test_world.h"

namespace z13::net {
namespace {

using z13::gameplay::input::AdoptActionIds;
using z13::gameplay::input::DescribeActions;
using z13::gameplay::input::InputConfigLoader;
using z13::input::ActionInfo;
using z13::input::ActionMap;
using z13::testing::kConnectArg;
using z13::testing::kMaxNetTestTicks;
using z13::testing::kNetTestDeltaTime;
using z13::testing::kServerArg;
using z13::testing::kTestServerEndpoint;
using z13::testing::KeyDown;
using z13::testing::KeyUp;
using z13::testing::RunNetworkUntil;
using z13::testing::Z13TestWorld;

constexpr uint32_t kClientAId = 1;
constexpr std::string_view kServerEnum = "test.ServerOnly";
constexpr std::string_view kClientEnum = "test.ClientOnly";
constexpr std::string_view kSharedEnum = "test.Shared";
constexpr std::string_view kActionName = "EXTRA";
constexpr ActionInfo::EnumValueType kServerValue = 11;
constexpr ActionInfo::EnumValueType kClientValue = 22;
constexpr uint32_t kIdShift = 10;

Z13TestWorld MakeServer(const std::shared_ptr<InMemoryNetwork>& network) {
  return Z13TestWorld({std::string(kServerArg)}, network);
}

Z13TestWorld MakeClient(const std::shared_ptr<InMemoryNetwork>& network) {
  return Z13TestWorld({std::string(kConnectArg), std::string(kTestServerEndpoint)}, network);
}

bool IsConnected(Z13TestWorld& world) {return world.World().has<z13::gameplay::Gameplay>() &&
      world.World().get<ConnectionStatus>().state == ConnectionState::kConnected;
}

bool HasFailed(Z13TestWorld& world) {return world.World().get<ConnectionStatus>().state == ConnectionState::kFailed;
}

void AddActionFromMissingModule(flecs::world world, std::string_view enum_name, std::string_view value_name, ActionInfo::EnumValueType value) {
  auto& map = world.get_mut<ActionMap>();
  const ActionInfo::IdType id = map.action_map.get<ActionMap::IdTag>().rbegin()->id + 1;
  map.action_map.emplace_back(ActionInfo {
      .enum_name = enum_name,
      .value_name = value_name,
      .group_name = z13::input::kControlActionGroup,
      .display_text = {},
      .default_keycodes = {},
      .enum_value = value,
      .id = id,
  });
}

std::optional<ActionInfo::IdType> IdOf(Z13TestWorld& world, std::string_view enum_name, ActionInfo::EnumValueType value) {
  return InputConfigLoader::FindActionId(world.World().get<ActionMap>().action_map, enum_name, value);
}

void ShiftLocalIds(flecs::world world) {
  std::vector<uint32_t> shifted(DescribeActions(world.get<ActionMap>()).size());
  std::iota(shifted.begin(), shifted.end(), kIdShift);
  ASSERT_TRUE(AdoptActionIds(world, shifted).has_value());
}

ActionInfo::IdType MoveForwardId(Z13TestWorld& world) {
  return IdOf(world, z13::gameplay::input::kActionsEnumName, static_cast<ActionInfo::EnumValueType>(z13::fbs::actions::Action::MOVE_FORWARD))
      .value();
}

Eigen::Vector3f Position(Z13TestWorld& world, uint32_t player_id) {
  const flecs::entity player = world.World().lookup(z13::gameplay::PlayerEntityName(player_id).c_str());
  EXPECT_TRUE(player) << "no player " << player_id;
  return player ? z13::math::ExtractTranslation<float>(player.get<Eigen::Matrix4f>()) : Eigen::Vector3f::Zero();
}

TEST(ActionNegotiationNetTest, AClientOnlyActionGetsAServerIdThatDoesNotCollideWithTheServersOwn) {
  auto network = std::make_shared<InMemoryNetwork>();
  Z13TestWorld server = MakeServer(network);
  AddActionFromMissingModule(server.World(), kServerEnum, kActionName, kServerValue);
  Z13TestWorld client = MakeClient(network);
  AddActionFromMissingModule(client.World(), kClientEnum, kActionName, kClientValue);
  ASSERT_EQ(IdOf(server, kServerEnum, kServerValue), IdOf(client, kClientEnum, kClientValue))
      << "the two extra actions must collide on the same local id for this test to mean anything";

  ASSERT_TRUE(RunNetworkUntil(
      *network, {server, client}, kNetTestDeltaTime, kMaxNetTestTicks, [&] { return IsConnected(client); }));

  const auto server_id = IdOf(server, kClientEnum, kClientValue);
  ASSERT_TRUE(server_id.has_value()) << "the server never registered the client's action";
  EXPECT_NE(server_id, IdOf(server, kServerEnum, kServerValue));
  EXPECT_EQ(IdOf(client, kClientEnum, kClientValue), server_id);
  EXPECT_EQ(MoveForwardId(client), MoveForwardId(server));
}

TEST(ActionNegotiationNetTest, TwoClientsWithTheSameExtraActionShareOneServerId) {
  auto network = std::make_shared<InMemoryNetwork>();
  Z13TestWorld server = MakeServer(network);
  Z13TestWorld client_a = MakeClient(network);
  Z13TestWorld client_b = MakeClient(network);
  for (Z13TestWorld* client : {&client_a, &client_b}) {
    AddActionFromMissingModule(client->World(), kSharedEnum, kActionName, kClientValue);
  }

  ASSERT_TRUE(RunNetworkUntil(*network, {server, client_a, client_b}, kNetTestDeltaTime, kMaxNetTestTicks, [&] {
    return IsConnected(client_a) && IsConnected(client_b);
  }));

  const auto server_id = IdOf(server, kSharedEnum, kClientValue);
  ASSERT_TRUE(server_id.has_value());
  EXPECT_EQ(IdOf(client_a, kSharedEnum, kClientValue), server_id);
  EXPECT_EQ(IdOf(client_b, kSharedEnum, kClientValue), server_id);
}

TEST(ActionNegotiationNetTest, AClientWithShiftedLocalIdsStillDrivesItsPlayerOnEveryone) {
  auto network = std::make_shared<InMemoryNetwork>();
  Z13TestWorld server = MakeServer(network);
  Z13TestWorld client_a = MakeClient(network);
  Z13TestWorld client_b = MakeClient(network);

  ShiftLocalIds(client_a.World());
  ASSERT_NE(MoveForwardId(client_a), MoveForwardId(server));

  ASSERT_TRUE(RunNetworkUntil(*network, {server, client_a, client_b}, kNetTestDeltaTime, kMaxNetTestTicks, [&] {
    return IsConnected(client_a) && IsConnected(client_b);
  }));
  ASSERT_EQ(MoveForwardId(client_a), MoveForwardId(server)) << "the Welcome did not renumber the client";

  const float spawn_x = Position(server, kClientAId).x();
  client_a.EmitInput(KeyDown(z13::fbs::input::Keycode::KEY_W));
  ASSERT_TRUE(RunNetworkUntil(*network, {server, client_a, client_b}, kNetTestDeltaTime, kMaxNetTestTicks, [&] {
    return Position(server, kClientAId).x() - spawn_x > 0.5f && Position(client_b, kClientAId).x() - spawn_x > 0.5f;
  })) << "the client's key press did not move its player on the server and the other client";

  // Observers lag a held key (neutral prediction), so compare once it is released.
  constexpr int kSettleTicks = 30;
  client_a.EmitInput(KeyUp(z13::fbs::input::Keycode::KEY_W));
  RunNetworkUntil(*network, {server, client_a, client_b}, kNetTestDeltaTime, kSettleTicks, [] { return false; });
  EXPECT_NEAR(Position(client_a, kClientAId).x(), Position(server, kClientAId).x(), z13::testing::kTestEpsilon);
  EXPECT_NEAR(Position(client_b, kClientAId).x(), Position(server, kClientAId).x(), z13::testing::kTestEpsilon);
}

TEST(ActionNegotiationNetTest, ADifferentEnumValueForTheSameActionStillJoins) {
  auto network = std::make_shared<InMemoryNetwork>();
  Z13TestWorld server = MakeServer(network);
  AddActionFromMissingModule(server.World(), kSharedEnum, kActionName, kServerValue);
  Z13TestWorld client = MakeClient(network);
  AddActionFromMissingModule(client.World(), kSharedEnum, kActionName, kClientValue);

  ASSERT_TRUE(RunNetworkUntil(
      *network, {server, client}, kNetTestDeltaTime, kMaxNetTestTicks, [&] { return IsConnected(client); }));

  EXPECT_EQ(IdOf(client, kSharedEnum, kClientValue), IdOf(server, kSharedEnum, kServerValue));
}

TEST(ActionNegotiationNetTest, AClientWithTooManyActionsFailsTheJoinWithoutSpendingAPlayerId) {
  auto network = std::make_shared<InMemoryNetwork>();
  Z13TestWorld server = MakeServer(network);
  Z13TestWorld bad_client = MakeClient(network);
  std::deque<std::string> names;
  for (size_t i = 0; i < z13::gameplay::input::kMaxActionsPerClient; ++i) {
    AddActionFromMissingModule(
        bad_client.World(), kClientEnum, names.emplace_back(std::format("A{}", i)), static_cast<ActionInfo::EnumValueType>(i));
  }

  ASSERT_TRUE(RunNetworkUntil(
      *network, {server, bad_client}, kNetTestDeltaTime, kMaxNetTestTicks, [&] { return HasFailed(bad_client); }));
  EXPECT_FALSE(bad_client.World().has<z13::gameplay::Gameplay>());
  EXPECT_FALSE(bad_client.World().get<ConnectionStatus>().reason.empty());

  Z13TestWorld good_client = MakeClient(network);
  ASSERT_TRUE(RunNetworkUntil(
      *network, {server, good_client}, kNetTestDeltaTime, kMaxNetTestTicks, [&] { return IsConnected(good_client); }));
  EXPECT_EQ(good_client.World().get<z13::gameplay::LocalPlayer>().id, kClientAId);
}

}  // namespace
}  // namespace z13::net
