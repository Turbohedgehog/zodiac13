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
#include <vector>

#include <flecs.h>


#include <net_module/in_memory_transport.h>

#include <z13/components/gameplay.h>
#include <z13/components/net.h>
#include <z13/components/player.h>
#include <z13_module/gameplay/gameplay_entities.h>
#include <z13_tests/shipped_station.h>

#include "../support/test_network.h"
#include "../support/z13_test_world.h"

// A world goes away with its owner: nothing a module keeps (a system's lambda, a singleton)
// may hold a flecs::world copy, or the world outlives Core and its transport never says goodbye.
namespace z13::net {
namespace {

using z13::testing::kConnectArg;
using z13::testing::kMaxNetTestTicks;
using z13::testing::kNetTestDeltaTime;
using z13::testing::kServerArg;
using z13::testing::kStationSceneArg;
using z13::testing::kTestServerEndpoint;
using z13::testing::RunNetworkUntil;
using z13::testing::Z13TestWorld;

// A few frames, so every system has run and built whatever it keeps.
constexpr uint64_t kWarmUpTicks = 5;

void MarkFinalized(ecs_world_t*, void* finalized) {
  *static_cast<bool*>(finalized) = true;
}

void ExpectFinalizedOnDestruction(std::unique_ptr<Z13TestWorld> world, bool& finalized) {
  ecs_atfini(world->World(), MarkFinalized, &finalized);
  world.reset();
  EXPECT_TRUE(finalized) << "something still holds the world";
}

bool InGame(Z13TestWorld& client) {
  return client.World().has<z13::gameplay::Gameplay>() &&
         client.World().get<ConnectionStatus>().state == ConnectionState::kConnected;
}

TEST(WorldLifetimeTest, ASinglePlayerWorldIsFinalizedWithItsOwner) {
  auto world = std::make_unique<Z13TestWorld>();
  for (uint64_t tick = 0; tick < kWarmUpTicks; ++tick) {
    world->Tick();
  }
  bool finalized = false;
  ExpectFinalizedOnDestruction(std::move(world), finalized);
}

TEST(WorldLifetimeTest, SessionWorldsAreFinalizedWithTheirOwners) {
  auto network = std::make_shared<InMemoryNetwork>();
  auto server = std::make_unique<Z13TestWorld>(
      std::vector {std::string(kServerArg), std::string(kStationSceneArg), std::string(z13::testing::kSiteScene)},
      network);
  auto client = std::make_unique<Z13TestWorld>(
      std::vector {std::string(kConnectArg), std::string(kTestServerEndpoint)}, network);
  ASSERT_TRUE(RunNetworkUntil(*network, {*server, *client}, kNetTestDeltaTime, kMaxNetTestTicks,
                              [&] { return InGame(*client); }));

  bool client_finalized = false;
  ExpectFinalizedOnDestruction(std::move(client), client_finalized);
  bool server_finalized = false;
  ExpectFinalizedOnDestruction(std::move(server), server_finalized);
}

// The client's transport closes with its world, so the server needn't wait for a timeout.
TEST(WorldLifetimeTest, AClosedClientLeavesTheServerAtOnce) {
  auto network = std::make_shared<InMemoryNetwork>();
  Z13TestWorld server({std::string(kServerArg)}, network);
  auto client = std::make_unique<Z13TestWorld>(
      std::vector {std::string(kConnectArg), std::string(kTestServerEndpoint)}, network);
  ASSERT_TRUE(RunNetworkUntil(*network, {server, *client}, kNetTestDeltaTime, kMaxNetTestTicks,
                              [&] { return InGame(*client); }));
  const uint32_t id = client->World().get<z13::gameplay::LocalPlayer>().id.value();

  client.reset();
  EXPECT_TRUE(RunNetworkUntil(*network, {server}, kNetTestDeltaTime, kMaxNetTestTicks, [&] {
    return !server.World().lookup(z13::gameplay::PlayerEntityName(id).c_str());
  })) << "player " << id << " is still on the server";
}

}  // namespace
}  // namespace z13::net
