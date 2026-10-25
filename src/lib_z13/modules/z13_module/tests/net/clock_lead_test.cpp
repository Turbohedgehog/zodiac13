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

#include <lib_core/state/rollback.h>

#include <net_module/in_memory_transport.h>

#include <z13_settings/settings.h>

#include "../support/load_session.h"

// A client's clock runs ahead of the server's by the trip and the send interval, so its commands
// arrive before their tick and the server has nothing to roll back (docs/network-scaling-plan.md).
namespace z13::net {
namespace {

using z13::testing::LoadSession;

constexpr size_t kClients = 2;
constexpr z13::net::FaultConfig kLatency {.min_delay_ticks = 1, .max_delay_ticks = 2};
// The trip (up to kLatency.max_delay_ticks), the send interval (3) and the slack of two ticks the clock keeps.
constexpr uint64_t kEnoughLeadTicks = 7;
constexpr uint64_t kConnectTicks = 600;
constexpr uint64_t kWarmUpTicks = 120;
constexpr uint64_t kMeasuredTicks = 150;

uint64_t ServerRollbacksWithLead(uint64_t lead) {
  LoadSession session(kClients, {}, [lead](z13::Settings& settings) { settings.net->clock_lead_ticks = lead; });
  session.Network().SetFaultConfig(kLatency);
  EXPECT_TRUE(session.Connect(kConnectTicks));
  for (uint64_t tick = 0; tick < kWarmUpTicks; ++tick) {
    session.Tick();
  }
  const auto rollbacks = [&] { return session.Server().World().get<z13::flecs_tools::RollbackMetrics>().rollbacks; };
  const uint64_t before = rollbacks();
  for (uint64_t tick = 0; tick < kMeasuredTicks; ++tick) {
    session.Tick();
  }
  return rollbacks() - before;
}

TEST(ClockLeadTest, WithoutALeadTheServerRollsBackForItsClientsCommands) {
  EXPECT_GT(ServerRollbacksWithLead(0), 0u);
}

TEST(ClockLeadTest, ALeadCoveringTheTripSparesTheServerTheRollbacks) {
  EXPECT_EQ(ServerRollbacksWithLead(kEnoughLeadTicks), 0u);
}

}  // namespace
}  // namespace z13::net
