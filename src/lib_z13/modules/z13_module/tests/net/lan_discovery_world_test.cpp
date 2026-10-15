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

#include <chrono>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <lib_core/settings/config.h>
#include <lib_core/time/simulation_clock.h>
#include <net_module/discovery_messages.h>
#include <net_module/enet_transport.h>
#include <net_module/protocol.h>

#include <z13/components/net.h>

#include "../support/z13_test_world.h"

// A Join window opened from the main menu, where the simulation is frozen, finds a server
// answering on the game port. A real socket on kDefaultServerPort: a game running on this
// machine makes the server fail to bind.
namespace z13::net {
namespace {

using z13::testing::Z13TestWorld;

constexpr auto kWaitTimeout = std::chrono::seconds(5);
constexpr auto kWaitInterval = std::chrono::milliseconds(2);
constexpr std::string_view kServerName = "Menu test";

void AnswerQueries(Transport& server) {
  (void)server.Service();
  for (const Datagram& query : server.TakeDatagrams()) {
    const ServerInfo info {.name = std::string(kServerName), .protocol_version = kProtocolVersion,
                           .game_port = z13::kDefaultServerPort};
    if (const auto reply = ReplyTo(query.bytes, info)) {
      EXPECT_TRUE(server.SendDatagram(query.from, *reply).has_value());
    }
  }
}

TEST(LanDiscoveryWorldTest, TheMainMenusJoinWindowFindsAServerOnTheGamePort) {
  auto server = CreateEnetServerTransport(z13::kDefaultServerPort);
  ASSERT_TRUE(server.has_value()) << server.error();
  Z13TestWorld client(std::vector<std::string> {});
  client.Tick();
  ASSERT_TRUE(client.World().has<z13::flecs_tools::SimulationFrozen>());

  client.World().add<LanSearch>();
  const auto deadline = std::chrono::steady_clock::now() + kWaitTimeout;
  while (client.World().get<FoundServers>().servers.empty() && std::chrono::steady_clock::now() < deadline) {
    client.Tick();
    AnswerQueries(**server);
    std::this_thread::sleep_for(kWaitInterval);
  }

  const auto& found = client.World().get<FoundServers>().servers;
  ASSERT_FALSE(found.empty());
  EXPECT_EQ(found.front().name, kServerName);
  EXPECT_EQ(found.front().endpoint.port, z13::kDefaultServerPort);
}

}  // namespace
}  // namespace z13::net
