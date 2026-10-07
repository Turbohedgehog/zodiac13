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
#include <cstdlib>
#include <memory>
#include <string>

#include <Eigen/Dense>

#include <lib_core/settings/config.h>
#include <lib_core/utils/math.h>

#include <net_module/in_memory_transport.h>

#include <z13/components/gameplay.h>
#include <z13/components/net.h>
#include <z13_module/gameplay/gameplay_entities.h>

#include "../support/building_test_helpers.h"
#include "../support/test_network.h"
#include "../support/z13_test_world.h"

// Session lifecycle driven from the menu (docs/client-server-plan.md, stage 6).
namespace z13::net {
namespace {

using z13::gameplay::Gameplay;
using z13::gameplay::Pause;
using z13::gameplay::PlayerEntityName;
using z13::testing::kConnectArg;
using z13::testing::kMaxNetTestTicks;
using z13::testing::kNetTestDeltaTime;
using z13::testing::kServerArg;
using z13::testing::kTestServerEndpoint;
using z13::testing::RunNetworkUntil;
using z13::testing::Z13TestWorld;

constexpr uint64_t kHoldTicks = 30;

Z13TestWorld MakeMenuWorld(const std::shared_ptr<InMemoryNetwork>& network) {return Z13TestWorld({}, network);
}

Z13TestWorld MakeServer(const std::shared_ptr<InMemoryNetwork>& network) {
  return Z13TestWorld({std::string(kServerArg)}, network);
}

Z13TestWorld MakeClient(const std::shared_ptr<InMemoryNetwork>& network) {
  return Z13TestWorld({std::string(kConnectArg), std::string(kTestServerEndpoint)}, network);
}

ConnectionState StatusOf(Z13TestWorld& world) {return world.World().get<ConnectionStatus>().state;
}

bool IsConnected(Z13TestWorld& world) {
  return world.World().has<Gameplay>() && StatusOf(world) == ConnectionState::kConnected;
}

bool HasPlayer(Z13TestWorld& world, uint32_t id) {return world.World().lookup(PlayerEntityName(id).c_str());
}

Eigen::Vector3f PositionOf(Z13TestWorld& world, uint32_t id) {return z13::math::ExtractTranslation<float>(
      world.World().lookup(PlayerEntityName(id).c_str()).get<Eigen::Matrix4f>());
}

void RunTicks(InMemoryNetwork& network, std::initializer_list<std::reference_wrapper<Z13TestWorld>> worlds,
              uint64_t ticks) {
  RunNetworkUntil(network, worlds, kNetTestDeltaTime, ticks, [] { return false; });
}

TEST(ListenServerTest, HostCommandsReachClientsAndMatchTheServer) {
  auto network = std::make_shared<InMemoryNetwork>();
  Z13TestWorld server = MakeServer(network);
  Z13TestWorld client = MakeClient(network);
  ASSERT_TRUE(RunNetworkUntil(
      *network, {server, client}, kNetTestDeltaTime, kMaxNetTestTicks, [&] { return IsConnected(client); }));
  const Eigen::Vector3f start = PositionOf(server, 0);

  server.EmitInput(z13::testing::KeyDown(z13::fbs::input::Keycode::KEY_W));
  RunTicks(*network, {server, client}, kHoldTicks);
  server.EmitInput(z13::testing::KeyUp(z13::fbs::input::Keycode::KEY_W));
  RunTicks(*network, {server, client}, kHoldTicks);

  ASSERT_GT((PositionOf(server, 0) - start).norm(), z13::testing::kTestEpsilon) << "the host never moved";
  EXPECT_LT((PositionOf(client, 0) - PositionOf(server, 0)).norm(), z13::testing::kTestEpsilon);
}

TEST(ListenServerTest, StartServerFromTheMenuHostsAJoinableGame) {
  auto network = std::make_shared<InMemoryNetwork>();
  Z13TestWorld host = MakeMenuWorld(network);
  ASSERT_TRUE(host.World().has<Pause>());

  host.World().entity().set<StartServerRequest>({.port = z13::kDefaultServerPort});
  host.Tick(kNetTestDeltaTime);

  EXPECT_TRUE(host.World().has<ServerRole>());
  EXPECT_TRUE(host.World().has<Gameplay>());
  EXPECT_FALSE(host.World().has<Pause>());
  EXPECT_TRUE(HasPlayer(host, 0));

  Z13TestWorld client = MakeClient(network);
  ASSERT_TRUE(RunNetworkUntil(
      *network, {host, client}, kNetTestDeltaTime, kMaxNetTestTicks, [&] { return IsConnected(client); }));
  EXPECT_TRUE(HasPlayer(host, 1));
}

TEST(ListenServerTest, StartServerOnABusyPortFailsWithoutAScene) {
  auto network = std::make_shared<InMemoryNetwork>();
  Z13TestWorld server = MakeServer(network);
  Z13TestWorld host = MakeMenuWorld(network);

  host.World().entity().set<StartServerRequest>({.port = z13::kDefaultServerPort});
  host.Tick(kNetTestDeltaTime);

  EXPECT_EQ(StatusOf(host), ConnectionState::kFailed);
  EXPECT_FALSE(host.World().get<ConnectionStatus>().reason.empty());
  EXPECT_FALSE(host.World().has<ServerRole>());
  EXPECT_FALSE(host.World().has<Gameplay>());
  EXPECT_TRUE(host.World().has<Pause>());
}

TEST(ListenServerTest, ServerFlagOnABusyPortExitsWithAnError) {
  auto network = std::make_shared<InMemoryNetwork>();
  Z13TestWorld server = MakeServer(network);
  Z13TestWorld second = MakeServer(network);

  EXPECT_EQ(StatusOf(second), ConnectionState::kFailed);
  EXPECT_FALSE(second.World().has<Gameplay>());
  EXPECT_TRUE(second.Core().IsPendingShutDown());
  EXPECT_EQ(second.Core().ExitCode(), EXIT_FAILURE);
  EXPECT_FALSE(server.Core().IsPendingShutDown());
}

TEST(ListenServerTest, HostExitRightAfterAJoinLeavesNoPlayerBehind) {
  auto network = std::make_shared<InMemoryNetwork>();
  Z13TestWorld server = MakeServer(network);
  Z13TestWorld client = MakeClient(network);
  ASSERT_TRUE(RunNetworkUntil(*network, {server, client}, kNetTestDeltaTime, kMaxNetTestTicks, [&] {
    return client.World().get<z13::gameplay::LocalPlayer>().id.has_value();
  }));
  ASSERT_FALSE(HasPlayer(server, 1)) << "the join must still be pending for this test to mean anything";

  server.World().remove<Gameplay>();
  RunTicks(*network, {server, client}, kHoldTicks);
  EXPECT_FALSE(HasPlayer(server, 1));

  server.World().add<Gameplay>();
  server.Tick(kNetTestDeltaTime);
  EXPECT_TRUE(HasPlayer(server, 0));
  EXPECT_FALSE(HasPlayer(server, 1));
}

TEST(ListenServerTest, ClientExitWhileCatchingUpEndsTheSession) {
  auto network = std::make_shared<InMemoryNetwork>();
  Z13TestWorld server = MakeServer(network);
  Z13TestWorld client = MakeClient(network);
  ASSERT_TRUE(RunNetworkUntil(*network, {server, client}, kNetTestDeltaTime, kMaxNetTestTicks, [&] {
    return client.World().get<z13::gameplay::LocalPlayer>().id.has_value();
  }));
  ASSERT_EQ(StatusOf(client), ConnectionState::kConnecting);
  ASSERT_TRUE(client.World().has<Gameplay>());

  client.World().remove<Gameplay>();
  RunTicks(*network, {server, client}, kHoldTicks);

  EXPECT_EQ(StatusOf(client), ConnectionState::kNone);
  EXPECT_FALSE(client.World().has<ClientRole>());
  EXPECT_FALSE(HasPlayer(client, 1));
  EXPECT_FALSE(HasPlayer(server, 1));
}

TEST(ListenServerTest, HostExitToMainMenuSendsClientsBackToTheirMenu) {
  auto network = std::make_shared<InMemoryNetwork>();
  Z13TestWorld server = MakeServer(network);
  Z13TestWorld client = MakeClient(network);
  ASSERT_TRUE(RunNetworkUntil(
      *network, {server, client}, kNetTestDeltaTime, kMaxNetTestTicks, [&] { return IsConnected(client); }));

  server.World().remove<Gameplay>();
  ASSERT_TRUE(RunNetworkUntil(*network, {server, client}, kNetTestDeltaTime, kMaxNetTestTicks, [&] {
    return StatusOf(client) == ConnectionState::kDisconnected;
  }));

  EXPECT_FALSE(server.World().has<ServerRole>());
  EXPECT_EQ(StatusOf(server), ConnectionState::kNone);
  EXPECT_FALSE(client.World().has<ClientRole>());
  EXPECT_FALSE(client.World().has<Gameplay>());
  EXPECT_TRUE(client.World().has<Pause>());
  EXPECT_FALSE(client.World().get<ConnectionStatus>().reason.empty());

  // The port is free again, and a single-player game after it spawns its own player.
  server.World().entity().set<StartServerRequest>({.port = z13::kDefaultServerPort});
  server.Tick(kNetTestDeltaTime);
  EXPECT_TRUE(server.World().has<ServerRole>());
}

TEST(ListenServerTest, ClientExitToMainMenuLeavesAndCanRejoin) {
  auto network = std::make_shared<InMemoryNetwork>();
  Z13TestWorld server = MakeServer(network);
  Z13TestWorld client = MakeClient(network);
  ASSERT_TRUE(RunNetworkUntil(
      *network, {server, client}, kNetTestDeltaTime, kMaxNetTestTicks, [&] { return IsConnected(client); }));

  client.World().remove<Gameplay>();
  ASSERT_TRUE(RunNetworkUntil(
      *network, {server, client}, kNetTestDeltaTime, kMaxNetTestTicks, [&] { return !HasPlayer(server, 1); }));
  EXPECT_FALSE(client.World().has<ClientRole>());
  EXPECT_EQ(StatusOf(client), ConnectionState::kNone);

  client.World().entity().set<JoinRequest>({.endpoint = {.host = "127.0.0.1", .port = z13::kDefaultServerPort}});
  ASSERT_TRUE(RunNetworkUntil(
      *network, {server, client}, kNetTestDeltaTime, kMaxNetTestTicks, [&] { return IsConnected(client); }));
  EXPECT_EQ(client.World().get<z13::gameplay::LocalPlayer>().id, 2u) << "ids are never reused";
}

TEST(ListenServerTest, LeaveRequestCancelsAJoinInProgress) {
  auto network = std::make_shared<InMemoryNetwork>();
  Z13TestWorld server = MakeServer(network);
  Z13TestWorld client = MakeClient(network);
  ASSERT_EQ(StatusOf(client), ConnectionState::kConnecting);

  client.World().entity().add<LeaveRequest>();
  RunTicks(*network, {server, client}, kHoldTicks);

  EXPECT_EQ(StatusOf(client), ConnectionState::kNone);
  EXPECT_FALSE(client.World().has<ClientRole>());
  EXPECT_FALSE(client.World().has<Gameplay>());
  EXPECT_FALSE(HasPlayer(server, 1));
}

}  // namespace
}  // namespace z13::net
