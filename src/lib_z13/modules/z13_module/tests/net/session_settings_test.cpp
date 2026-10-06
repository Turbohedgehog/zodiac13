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

#include <memory>
#include <string>

#include <z13_settings/settings.h>

#include <net_module/in_memory_transport.h>
#include <z13/components/gameplay.h>
#include <z13/components/net.h>

#include "../support/test_network.h"
#include "../support/z13_test_world.h"

namespace z13::net {
namespace {

using z13::gameplay::Gameplay;
using z13::testing::kConnectArg;
using z13::testing::kMaxNetTestTicks;
using z13::testing::kNetTestDeltaTime;
using z13::testing::kServerArg;
using z13::testing::kTestServerEndpoint;
using z13::testing::RunNetworkUntil;
using z13::testing::Z13TestWorld;

Settings ServerSettings() {
  Settings settings = MakeSettings();
  settings.core->fps = 30.;
  settings.net->max_late_ticks = 50;
  settings.net->send_interval_ticks = 5;
  settings.physics->player_collider_radius = 0.25f;
  settings.physics->max_sweep_iterations = 5;
  settings.building->destroy_reach_distance = 3.f;
  settings.building->spawn_clearance_cells = 12;
  return settings;
}

Z13TestWorld MakeServer(const std::shared_ptr<InMemoryNetwork>& network, const Settings& settings) {
  return Z13TestWorld(/*skip_main_menu=*/false, {std::string(kServerArg)}, network, settings);
}

Z13TestWorld MakeClient(const std::shared_ptr<InMemoryNetwork>& network, const Settings& settings = MakeSettings()) {
  return Z13TestWorld(
      /*skip_main_menu=*/false, {std::string(kConnectArg), std::string(kTestServerEndpoint)}, network, settings);
}

bool IsConnected(Z13TestWorld& world) {
  return world.World().has<Gameplay>() &&
      world.World().get<ConnectionStatus>().state == ConnectionState::kConnected;
}

TEST(SessionSettingsTest, ClientRunsOnTheServersFpsAndTuning) {
  auto network = std::make_shared<InMemoryNetwork>();
  const Settings server_settings = ServerSettings();
  Z13TestWorld server = MakeServer(network, server_settings);
  Z13TestWorld client = MakeClient(network);
  ASSERT_EQ(client.Config().GetFPS(), CoreSettings {}.fps) << "own settings until the join";

  ASSERT_TRUE(RunNetworkUntil(
      *network, {server, client}, kNetTestDeltaTime, kMaxNetTestTicks, [&] { return IsConnected(client); }));

  EXPECT_EQ(client.Config().GetFPS(), server_settings.core->fps);
  EXPECT_EQ(client.World().get<NetTuning>(), *server_settings.net);
  EXPECT_EQ(client.World().get<PhysicsTuning>(), *server_settings.physics);
  EXPECT_EQ(client.World().get<BuildingTuning>(), *server_settings.building);
  EXPECT_EQ(client.Config().GetCoreSettings().fps, CoreSettings {}.fps) << "the configured rate stays as it was";
  EXPECT_EQ(server.World().get<NetTuning>(), *server_settings.net) << "the server keeps its own";
}

TEST(SessionSettingsTest, LeavingGivesTheClientItsOwnSettingsBack) {
  auto network = std::make_shared<InMemoryNetwork>();
  Z13TestWorld server = MakeServer(network, ServerSettings());
  Z13TestWorld client = MakeClient(network);
  ASSERT_TRUE(RunNetworkUntil(
      *network, {server, client}, kNetTestDeltaTime, kMaxNetTestTicks, [&] { return IsConnected(client); }));

  client.World().remove<Gameplay>();
  ASSERT_TRUE(RunNetworkUntil(*network, {server, client}, kNetTestDeltaTime, kMaxNetTestTicks, [&] {
    return client.World().get<ConnectionStatus>().state == ConnectionState::kNone;
  }));

  EXPECT_EQ(client.Config().GetFPS(), CoreSettings {}.fps);
  EXPECT_EQ(client.World().get<NetTuning>(), NetTuning {});
  EXPECT_EQ(client.World().get<PhysicsTuning>(), PhysicsTuning {});
  EXPECT_EQ(client.World().get<BuildingTuning>(), BuildingTuning {});
}

TEST(SessionSettingsTest, ClientRefusesServerSettingsItsHistoryCannotCover) {
  auto network = std::make_shared<InMemoryNetwork>();
  Settings server_settings = MakeSettings();
  server_settings.core->snapshot_retention_seconds = 10.;
  server_settings.net->max_late_ticks = 400;  // 6.7s + 1s of history at 60 fps
  Settings client_settings = MakeSettings();
  client_settings.core->snapshot_retention_seconds = 3.;
  Z13TestWorld server = MakeServer(network, server_settings);
  Z13TestWorld client = MakeClient(network, client_settings);

  ASSERT_TRUE(RunNetworkUntil(*network, {server, client}, kNetTestDeltaTime, kMaxNetTestTicks, [&] {
    return client.World().get<ConnectionStatus>().state == ConnectionState::kFailed;
  }));

  EXPECT_FALSE(client.World().has<Gameplay>());
  EXPECT_EQ(client.World().get<NetTuning>(), NetTuning {}) << "nothing adopted from a refused join";
}

}  // namespace
}  // namespace z13::net
