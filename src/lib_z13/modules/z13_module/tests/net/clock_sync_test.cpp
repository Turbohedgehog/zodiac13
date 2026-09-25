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

#include <lib_core/simulation_clock.h>

#include <net_module/clock_sync.h>
#include <net_module/in_memory_transport.h>

#include <z13/components/gameplay.h>
#include <z13/components/net.h>

#include "../support/test_network.h"
#include "../support/z13_test_world.h"

// A client schedules its commands against the server's clock, so the whole command
// stage rests on this estimate (docs/client-server-plan.md, "Пинг").
namespace z13::net {
namespace {

namespace ft = z13::flecs_tools;
using z13::testing::RunNetworkUntil;
using z13::testing::Z13TestWorld;

constexpr float kTestDeltaTime = 1.f / 60.f;
constexpr uint64_t kMaxTicks = 600;
constexpr std::string_view kServerEndpoint = "127.0.0.1:26213";
// One Ping per second, so a few seconds of ticking is a few samples.
constexpr uint64_t kSecondsToSettle = 4;

Z13TestWorld MakeServer(const std::shared_ptr<InMemoryNetwork>& network) {
  return Z13TestWorld(/*skip_main_menu=*/false, {"--server"}, network);
}

Z13TestWorld MakeClient(const std::shared_ptr<InMemoryNetwork>& network) {
  return Z13TestWorld(/*skip_main_menu=*/false, {"--connect", std::string(kServerEndpoint)}, network);
}

bool IsConnected(Z13TestWorld& world) {
  return world.World().has<z13::gameplay::Gameplay>() &&
      world.World().get<ConnectionStatus>().state == ConnectionState::kConnected;
}

uint64_t Tick(Z13TestWorld& world) {
  return world.World().get<ft::SimulationClock>().tick;
}

// How far the server's clock really is ahead of the client's, which is what the
// estimate is supposed to recover.
int64_t RealOffset(Z13TestWorld& server, Z13TestWorld& client) {
  return static_cast<int64_t>(Tick(server)) - static_cast<int64_t>(Tick(client));
}

// ---------- pure functions ----------

TEST(ClockSyncTest, ScheduleTickAddsTheOffsetAndTheInputDelay) {
  EXPECT_EQ(ScheduleTick(100, std::nullopt), 100u + kInputDelayTicks);
  EXPECT_EQ(ScheduleTick(100, 30), 130u + kInputDelayTicks);
  EXPECT_EQ(ScheduleTick(100, -30), 70u + kInputDelayTicks);
}

// A client that has barely started can hold an offset larger than its own tick.
TEST(ClockSyncTest, ScheduleTickClampsInsteadOfWrapping) {
  EXPECT_EQ(ScheduleTick(0, -1000), 0u);
}

TEST(ClockSyncTest, TheFirstSampleIsTakenWholeAndLaterOnesAreSmoothed) {
  ClockSync sync;
  sync.ping_sent_tick = 100;
  // Round trip of 4 ticks, server 50 ahead: stamped at 100 + 50 + 2.
  ApplyPong(sync, 100, 152, 104);
  ASSERT_TRUE(sync.offset_ticks.has_value());
  EXPECT_EQ(*sync.offset_ticks, 50);
  EXPECT_EQ(sync.rtt_ticks, 4);
  EXPECT_FALSE(sync.ping_sent_tick.has_value());

  // A second, quite different sample must not replace the estimate outright.
  sync.ping_sent_tick = 200;
  ApplyPong(sync, 200, 302, 204);
  EXPECT_GT(*sync.offset_ticks, 50);
  EXPECT_LT(*sync.offset_ticks, 100);
}

TEST(ClockSyncTest, AReplyToAnotherPingIsIgnored) {
  ClockSync sync;
  sync.ping_sent_tick = 300;
  ApplyPong(sync, 100, 152, 304);

  EXPECT_FALSE(sync.offset_ticks.has_value());
  EXPECT_EQ(sync.ping_sent_tick, 300u);  // still waiting for its own reply
}

// The point of smoothing: one delayed Pong must not drag the schedule of every command
// after it.
TEST(ClockSyncTest, OneSpikeMovesTheEstimateOnlyPartway) {
  ClockSync sync;
  for (uint64_t sent = 100; sent <= 400; sent += 100) {
    sync.ping_sent_tick = sent;
    ApplyPong(sync, sent, sent + 52, sent + 4);
  }
  ASSERT_EQ(*sync.offset_ticks, 50);

  sync.ping_sent_tick = 500;
  ApplyPong(sync, 500, 650, 504);  // a sample 100 ticks off

  EXPECT_GT(*sync.offset_ticks, 50);
  EXPECT_LT(*sync.offset_ticks, 100) << "a single spike moved the estimate more than half way";
}

// ---------- over a real session ----------

TEST(ClockSyncTest, TheEstimateFindsTheRealOffsetAcrossAFixedDelay) {
  auto network = std::make_shared<InMemoryNetwork>();
  network->SetFaultConfig({.min_delay_ticks = 3, .max_delay_ticks = 3});

  Z13TestWorld server = MakeServer(network);
  for (uint64_t i = 0; i < 90; ++i) {
    server.World().progress(kTestDeltaTime);  // put the two clocks visibly apart
  }

  Z13TestWorld client = MakeClient(network);
  ASSERT_TRUE(RunNetworkUntil(
      *network, {server, client}, kTestDeltaTime, kMaxTicks, [&] { return IsConnected(client); }));

  ASSERT_TRUE(RunNetworkUntil(*network, {server, client}, kTestDeltaTime, kMaxTicks, [&] {
    return client.World().get<ClockSync>().offset_ticks.has_value();
  }));
  RunNetworkUntil(*network, {server, client}, kTestDeltaTime, 60 * kSecondsToSettle, [] { return false; });

  const std::optional<int64_t> estimate = client.World().get<ClockSync>().offset_ticks;
  ASSERT_TRUE(estimate.has_value());
  EXPECT_LE(std::abs(*estimate - RealOffset(server, client)), 2)
      << "estimate " << *estimate << " vs real " << RealOffset(server, client);
}

// Ping/Pong ride the unreliable channel, so losing them all costs the estimate and
// nothing else: the session itself is reliable traffic and carries on.
TEST(ClockSyncTest, LosingEveryPingLeavesTheSessionAloneAndTheNextOneRecovers) {
  auto network = std::make_shared<InMemoryNetwork>();
  network->SetFaultConfig({.drop_probability = 1.0});

  Z13TestWorld server = MakeServer(network);
  Z13TestWorld client = MakeClient(network);
  ASSERT_TRUE(RunNetworkUntil(
      *network, {server, client}, kTestDeltaTime, kMaxTicks, [&] { return IsConnected(client); }));

  RunNetworkUntil(*network, {server, client}, kTestDeltaTime, 60 * kSecondsToSettle, [] { return false; });
  EXPECT_FALSE(client.World().get<ClockSync>().offset_ticks.has_value());
  EXPECT_TRUE(IsConnected(client)) << "dropping unreliable packets must not disturb the session";

  network->SetFaultConfig({});
  EXPECT_TRUE(RunNetworkUntil(*network, {server, client}, kTestDeltaTime, kMaxTicks, [&] {
    return client.World().get<ClockSync>().offset_ticks.has_value();
  }));
}

}  // namespace
}  // namespace z13::net
