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
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>

#include <lib_core/settings/core_settings.h>
#include <lib_core/time/simulation_clock.h>

#include <net_module/clock_sync.h>
#include <net_module/in_memory_transport.h>

#include <z13/components/gameplay.h>
#include <z13/components/player.h>
#include <z13/components/net.h>
#include <z13/components/player_action.h>

#include "../support/building_test_helpers.h"
#include "../support/test_network.h"
#include "../support/z13_test_world.h"

namespace z13::net {
namespace {

const NetTuning kTuning;
const int64_t kClockCatchUpThresholdTicks = kTuning.clock_catch_up_threshold_ticks;
const uint64_t kClockCatchUpEveryTicks = kTuning.clock_catch_up_every_ticks;
const int64_t kClockJumpThresholdTicks = kTuning.clock_jump_threshold_ticks;
const uint64_t kMaxScheduleAheadTicks = kTuning.max_schedule_ahead_ticks;
const uint64_t kMaxLateTicks = kTuning.max_late_ticks;

namespace ft = z13::flecs_tools;
using z13::testing::kConnectArg;
using z13::testing::kServerArg;
using z13::testing::kMaxNetTestTicks;
using z13::testing::kNetTestDeltaTime;
using z13::testing::kTestServerEndpoint;
using z13::testing::RunNetworkUntil;
using z13::testing::Z13TestWorld;

constexpr uint64_t kSecondsToSettle = 4;

Z13TestWorld MakeServer(const std::shared_ptr<InMemoryNetwork>& network) {
  return Z13TestWorld({std::string(kServerArg)}, network);
}

Z13TestWorld MakeClient(const std::shared_ptr<InMemoryNetwork>& network) {
  return Z13TestWorld({std::string(kConnectArg), std::string(kTestServerEndpoint)}, network);
}

bool IsConnected(Z13TestWorld& world) {return world.World().has<z13::gameplay::Gameplay>() &&
      world.World().get<ConnectionStatus>().state == ConnectionState::kConnected;
}

uint64_t Tick(Z13TestWorld& world) {return world.World().get<ft::SimulationClock>().tick;
}

int64_t RealOffset(Z13TestWorld& server, Z13TestWorld& client) {
  return static_cast<int64_t>(Tick(server)) - static_cast<int64_t>(Tick(client));
}

TEST(ClockSyncTest, ASmallOffsetIsClosedOneTickPerCadenceStep) {
  ClockSync sync;
  sync.offset_ticks = kClockCatchUpThresholdTicks;

  EXPECT_EQ(TakeClockAdjustment(sync, kTuning, kClockCatchUpEveryTicks + 1), 0) << "off the cadence";
  ASSERT_EQ(TakeClockAdjustment(sync, kTuning, kClockCatchUpEveryTicks), 1);
  EXPECT_EQ(sync.offset_ticks, kClockCatchUpThresholdTicks - 1);
  EXPECT_EQ(TakeClockAdjustment(sync, kTuning, 2 * kClockCatchUpEveryTicks), 0) << "under the threshold";

  sync.offset_ticks = -kClockCatchUpThresholdTicks;
  EXPECT_EQ(TakeClockAdjustment(sync, kTuning, kClockCatchUpEveryTicks), -1) << "ahead: skip a tick";
  EXPECT_EQ(sync.offset_ticks, -kClockCatchUpThresholdTicks + 1);
}

TEST(ClockSyncTest, ALargeOffsetIsClosedAtOnceEitherWay) {
  ClockSync sync;
  sync.offset_ticks = 120;
  EXPECT_EQ(TakeClockAdjustment(sync, kTuning, kClockCatchUpEveryTicks + 1), 120);
  EXPECT_EQ(sync.offset_ticks, 0);

  sync.offset_ticks = -kClockJumpThresholdTicks;
  EXPECT_EQ(TakeClockAdjustment(sync, kTuning, kClockCatchUpEveryTicks + 1), -kClockJumpThresholdTicks);
  EXPECT_EQ(sync.offset_ticks, 0);
}

// Mid-jump: 50 of 200 extra ticks have run, which a round trip of 4 ticks includes.
TEST(ClockSyncTest, APongDuringAJumpCountsTheTicksStillToRun) {
  ClockSync sync;
  sync.ping_sent_tick = 100;

  ApplyPong(sync, kTuning, 100, 354, 154, {.ticks = 150, .applied = 50});

  EXPECT_EQ(sync.rtt_ticks, 4);
  EXPECT_EQ(sync.offset_ticks, 52);
}

TEST(ClockSyncTest, AnUnmeasuredClockIsLeftAlone) {
  ClockSync sync;
  EXPECT_EQ(TakeClockAdjustment(sync, kTuning, kClockCatchUpEveryTicks), 0);
}

TEST(ClockSyncTest, AdjustmentsDuringAPingDoNotCountAsRoundTrip) {
  ClockSync sync;
  sync.offset_ticks = 10;
  sync.ping_sent_tick = 100;
  ASSERT_EQ(TakeClockAdjustment(sync, kTuning, kClockCatchUpEveryTicks), 1);
  ASSERT_EQ(TakeClockAdjustment(sync, kTuning, 2 * kClockCatchUpEveryTicks), 1);

  // 4 ticks of real round trip plus the 2 extra ticks run meanwhile.
  ApplyPong(sync, kTuning, 100, 110, 106, {.applied = 2});
  EXPECT_EQ(sync.rtt_ticks, 4);

  sync.offset_ticks = -kClockJumpThresholdTicks;
  sync.ping_sent_tick = 200;
  sync.applied_at_ping = 2;
  ASSERT_EQ(TakeClockAdjustment(sync, kTuning, 1), -kClockJumpThresholdTicks);
  // 4 counted ticks plus the skipped frames, which the clock never counted.
  ApplyPong(sync, kTuning, 200, 210, 204, {.applied = 2 - kClockJumpThresholdTicks});
  EXPECT_EQ(sync.rtt_ticks, 4 + kClockJumpThresholdTicks);
}

TEST(ClockSyncTest, ALaggingClientCatchesUpWithoutJumping) {
  constexpr uint32_t kLatencyTicks = 10;
  constexpr uint64_t kMaxTicksPerFrame = 2;
  auto network = std::make_shared<InMemoryNetwork>();
  network->SetFaultConfig({.min_delay_ticks = kLatencyTicks, .max_delay_ticks = kLatencyTicks});

  Z13TestWorld server = MakeServer(network);
  Z13TestWorld client = MakeClient(network);
  ASSERT_TRUE(RunNetworkUntil(*network, {server, client}, kNetTestDeltaTime, kMaxNetTestTicks, [&] {
    return IsConnected(client) && client.World().get<ClockSync>().offset_ticks.has_value();
  }));
  ASSERT_GT(RealOffset(server, client), kClockCatchUpThresholdTicks + 1) << "joined already caught up";

  uint64_t previous = Tick(client);
  uint64_t max_step = 0;
  RunNetworkUntil(*network, {server, client}, kNetTestDeltaTime, 60 * kSecondsToSettle, [&] {
    max_step = std::max(max_step, Tick(client) - previous);
    previous = Tick(client);
    return false;
  });

  EXPECT_LE(max_step, kMaxTicksPerFrame);
  EXPECT_LE(RealOffset(server, client), kClockCatchUpThresholdTicks + 1);
  EXPECT_GE(RealOffset(server, client), -kClockCatchUpThresholdTicks);
}

size_t LoggedFor(Z13TestWorld& world, uint32_t player_id) {return static_cast<size_t>(std::ranges::count_if(
      world.World().get<z13::gameplay::PlayerActionLog>().log.Entries(),
      [player_id](const z13::gameplay::PlayerActionRecord& record) { return record.player_id == player_id; }));
}

// The other world never makes up the lost frames, like Core::Run.
void RunAlone(InMemoryNetwork& network, Z13TestWorld& running, uint64_t frames) {
  RunNetworkUntil(network, {running}, kNetTestDeltaTime, frames, [] { return false; });
}

void ExpectClockRecoversAndCommandsLand(
    InMemoryNetwork& network, Z13TestWorld& server, Z13TestWorld& client) {
  RunNetworkUntil(network, {server, client}, kNetTestDeltaTime, 60 * kSecondsToSettle, [] { return false; });
  EXPECT_LE(std::abs(RealOffset(server, client)), kClockCatchUpThresholdTicks + 1);

  const uint32_t id = *client.World().get<z13::gameplay::LocalPlayer>().id;
  client.EmitInput(z13::testing::KeyDown(z13::fbs::input::Keycode::KEY_W));
  EXPECT_TRUE(RunNetworkUntil(network, {server, client}, kNetTestDeltaTime, kMaxNetTestTicks, [&] {
    return LoggedFor(server, id) > 0;
  })) << "the server dropped the command";
}

TEST(ClockSyncTest, AServerHitchLeavesClientsAheadOnlyUntilTheNextPing) {
  constexpr uint64_t kHitchFrames = 60;
  auto network = std::make_shared<InMemoryNetwork>();
  Z13TestWorld server = MakeServer(network);
  Z13TestWorld client = MakeClient(network);
  ASSERT_TRUE(RunNetworkUntil(*network, {server, client}, kNetTestDeltaTime, kMaxNetTestTicks, [&] {
    return IsConnected(client) && client.World().get<ClockSync>().offset_ticks.has_value();
  }));

  RunAlone(*network, client, kHitchFrames);
  ASSERT_LT(RealOffset(server, client), -static_cast<int64_t>(kMaxScheduleAheadTicks)) << "the client should now be out of the window";

  ExpectClockRecoversAndCommandsLand(*network, server, client);
}

TEST(ClockSyncTest, AClientHitchIsClosedAtOnceNotOverSeconds) {
  constexpr uint64_t kHitchFrames = 120;
  auto network = std::make_shared<InMemoryNetwork>();
  Z13TestWorld server = MakeServer(network);
  Z13TestWorld client = MakeClient(network);
  ASSERT_TRUE(RunNetworkUntil(*network, {server, client}, kNetTestDeltaTime, kMaxNetTestTicks, [&] {
    return IsConnected(client) && client.World().get<ClockSync>().offset_ticks.has_value();
  }));

  RunAlone(*network, server, kHitchFrames);
  ASSERT_GT(RealOffset(server, client), static_cast<int64_t>(kMaxLateTicks));

  ExpectClockRecoversAndCommandsLand(*network, server, client);
}

// A lag longer than one frame's jump, measured by a Pong (Н11 in docs/net-core.md).
TEST(ClockSyncTest, AClientThatLoadedLongJumpsToTheServer) {
  constexpr uint64_t kBeyondOneJumpTicks = 200;
  auto network = std::make_shared<InMemoryNetwork>();
  Z13TestWorld server = MakeServer(network);
  Z13TestWorld client = MakeClient(network);
  ASSERT_TRUE(RunNetworkUntil(
      *network, {server, client}, kNetTestDeltaTime, kMaxNetTestTicks, [&] { return IsConnected(client); }));
  const uint64_t max_jump_ticks = client.World().get<z13::ActiveCoreSettings>().max_catch_up_ticks_per_frame;
  const uint64_t load_frames = max_jump_ticks + kBeyondOneJumpTicks;
  // A ping a second; the jump past one frame's cap finishes the frame after.
  const uint64_t frames_to_jump = 2 * *ft::TicksPerSecond(client.World());

  RunAlone(*network, server, load_frames);
  ASSERT_GT(RealOffset(server, client), static_cast<int64_t>(max_jump_ticks));

  uint64_t frames = 0;
  EXPECT_TRUE(RunNetworkUntil(*network, {server, client}, kNetTestDeltaTime, 60 * kSecondsToSettle, [&] {
    ++frames;
    return std::abs(RealOffset(server, client)) <= kClockCatchUpThresholdTicks + 1;
  }));
  EXPECT_LE(frames, frames_to_jump) << "caught up at the catch-up pace instead of jumping";
}

// What Core::Run reports after a long load or another hitch is made up in the next frames,
// a jump longer than one frame's included, not after the next Pong.
TEST(ClockSyncTest, AHitchIsMadeUpAtOnce) {
  constexpr uint64_t kBeyondOneJumpTicks = 200;
  constexpr uint64_t kFramesToMakeUp = 3;
  auto network = std::make_shared<InMemoryNetwork>();
  Z13TestWorld server = MakeServer(network);
  Z13TestWorld client = MakeClient(network);
  ASSERT_TRUE(RunNetworkUntil(
      *network, {server, client}, kNetTestDeltaTime, kMaxNetTestTicks, [&] { return IsConnected(client); }));
  const uint64_t hitch_ticks =
      client.World().get<z13::ActiveCoreSettings>().max_catch_up_ticks_per_frame + kBeyondOneJumpTicks;

  RunAlone(*network, server, hitch_ticks);
  client.World().get_mut<ft::DroppedTicks>().ticks = hitch_ticks;
  RunNetworkUntil(*network, {server, client}, kNetTestDeltaTime, kFramesToMakeUp, [] { return false; });

  EXPECT_LE(std::abs(RealOffset(server, client)), kClockCatchUpThresholdTicks + 1);
}

TEST(ClockSyncTest, TheFirstSampleIsTakenWholeAndLaterOnesAreSmoothed) {
  ClockSync sync;
  sync.ping_sent_tick = 100;
  // Round trip of 4 ticks, server 50 ahead: stamped at 100 + 50 + 2.
  ApplyPong(sync, kTuning, 100, 152, 104);
  ASSERT_TRUE(sync.offset_ticks.has_value());
  EXPECT_EQ(*sync.offset_ticks, 50);
  EXPECT_EQ(sync.rtt_ticks, 4);
  EXPECT_FALSE(sync.ping_sent_tick.has_value());

  // Within what latency jitter explains: smoothed, not taken whole.
  sync.ping_sent_tick = 200;
  ApplyPong(sync, kTuning, 200, 262, 204);
  EXPECT_GT(*sync.offset_ticks, 50);
  EXPECT_LT(*sync.offset_ticks, 60);
}

TEST(ClockSyncTest, AReplyToAnotherPingIsIgnored) {
  ClockSync sync;
  sync.ping_sent_tick = 300;
  ApplyPong(sync, kTuning, 100, 152, 304);

  EXPECT_FALSE(sync.offset_ticks.has_value());
  EXPECT_EQ(sync.ping_sent_tick, 300u);
}

TEST(ClockSyncTest, OneSpikeMovesTheEstimateOnlyPartway) {
  ClockSync sync;
  for (uint64_t sent = 100; sent <= 400; sent += 100) {
    sync.ping_sent_tick = sent;
    ApplyPong(sync, kTuning, sent, sent + 52, sent + 4);
  }
  ASSERT_EQ(*sync.offset_ticks, 50);

  // A 100-tick latency spike, all of it on the way back: the sample is off by rtt / 2.
  sync.ping_sent_tick = 500;
  ApplyPong(sync, kTuning, 500, 552, 604);

  EXPECT_LT(*sync.offset_ticks, 50);
  EXPECT_GT(*sync.offset_ticks, 25) << "a single spike moved the estimate more than half way";
}

TEST(ClockSyncTest, AMissLatencyCannotExplainIsTakenWhole) {
  ClockSync sync;
  sync.ping_sent_tick = 100;
  ApplyPong(sync, kTuning, 100, 152, 104);
  ASSERT_EQ(*sync.offset_ticks, 50);

  // Same round trip, server 100 ticks further: a clock moved.
  sync.ping_sent_tick = 200;
  ApplyPong(sync, kTuning, 200, 352, 204);
  EXPECT_EQ(*sync.offset_ticks, 150);
}


TEST(ClockSyncTest, TheEstimateFindsTheRealOffsetAcrossAFixedDelay) {
  auto network = std::make_shared<InMemoryNetwork>();
  network->SetFaultConfig({.min_delay_ticks = 3, .max_delay_ticks = 3});

  Z13TestWorld server = MakeServer(network);
  for (uint64_t i = 0; i < 90; ++i) {
    server.Tick(kNetTestDeltaTime);  // put the two clocks visibly apart
  }

  Z13TestWorld client = MakeClient(network);
  ASSERT_TRUE(RunNetworkUntil(
      *network, {server, client}, kNetTestDeltaTime, kMaxNetTestTicks, [&] { return IsConnected(client); }));

  ASSERT_TRUE(RunNetworkUntil(*network, {server, client}, kNetTestDeltaTime, kMaxNetTestTicks, [&] {
    return client.World().get<ClockSync>().offset_ticks.has_value();
  }));
  RunNetworkUntil(*network, {server, client}, kNetTestDeltaTime, 60 * kSecondsToSettle, [] { return false; });

  const std::optional<int64_t> estimate = client.World().get<ClockSync>().offset_ticks;
  ASSERT_TRUE(estimate.has_value());
  EXPECT_LE(std::abs(*estimate - RealOffset(server, client)), 2)
      << "estimate " << *estimate << " vs real " << RealOffset(server, client);
}

TEST(ClockSyncTest, LosingEveryPingLeavesTheSessionAloneAndTheNextOneRecovers) {
  auto network = std::make_shared<InMemoryNetwork>();
  network->SetFaultConfig({.drop_probability = 1.0});

  Z13TestWorld server = MakeServer(network);
  Z13TestWorld client = MakeClient(network);
  ASSERT_TRUE(RunNetworkUntil(
      *network, {server, client}, kNetTestDeltaTime, kMaxNetTestTicks, [&] { return IsConnected(client); }));

  RunNetworkUntil(*network, {server, client}, kNetTestDeltaTime, 60 * kSecondsToSettle, [] { return false; });
  EXPECT_FALSE(client.World().get<ClockSync>().offset_ticks.has_value());
  EXPECT_TRUE(IsConnected(client)) << "dropping unreliable packets must not disturb the session";

  network->SetFaultConfig({});
  EXPECT_TRUE(RunNetworkUntil(*network, {server, client}, kNetTestDeltaTime, kMaxNetTestTicks, [&] {
    return client.World().get<ClockSync>().offset_ticks.has_value();
  }));
}

}  // namespace
}  // namespace z13::net
