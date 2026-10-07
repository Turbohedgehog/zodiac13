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
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <lib_core/settings/config.h>

#include <net_module/in_memory_transport.h>

#include <z13/components/gameplay.h>
#include <z13/components/input.h>
#include <z13/components/net.h>
#include <z13_settings/net_tuning.h>

#include "../support/building_test_helpers.h"
#include "../support/test_network.h"
#include "../support/z13_test_world.h"

namespace z13::net {
namespace {

using z13::testing::kConnectArg;
using z13::testing::kMaxNetTestTicks;
using z13::testing::kNetTestDeltaTime;
using z13::testing::kServerArg;
using z13::testing::kTestServerEndpoint;
using z13::testing::KeyDown;
using z13::testing::KeyUp;
using z13::testing::RunNetworkUntil;
using z13::testing::Z13TestWorld;
using Keycode = z13::fbs::input::Keycode;

const NetTuning kTuning;
// Measured peak (mouse look) is ~1.8 KB/s.
const uint64_t kClientUplinkBudgetBytesPerSecond = kTuning.client_uplink_budget_bytes_per_second;
// Pings and clock sync only; measured at 48 B/s.
const uint64_t kIdleUplinkBudgetBytesPerSecond = kTuning.idle_uplink_budget_bytes_per_second;

constexpr uint64_t kTicksPerSecond = 60;
constexpr uint64_t kSessionSeconds = 10;
constexpr uint64_t kHoldForwardTicks = 5 * kTicksPerSecond;
constexpr uint64_t kMouseLookTicks = 2 * kTicksPerSecond;
constexpr uint64_t kIdleSeconds = 3;
constexpr int kMouseDeltaX = 5;
constexpr int kMouseJitterX = 5;
constexpr int kMouseJitterY = 3;

Z13TestWorld MakeServer(const std::shared_ptr<InMemoryNetwork>& network) {
  return Z13TestWorld({std::string(kServerArg)}, network);
}

Z13TestWorld MakeClient(const std::shared_ptr<InMemoryNetwork>& network) {
  return Z13TestWorld({std::string(kConnectArg), std::string(kTestServerEndpoint)}, network);
}

bool IsConnected(Z13TestWorld& world) {return world.World().has<z13::gameplay::Gameplay>() &&
      world.World().get<ConnectionStatus>().state == ConnectionState::kConnected;
}

// Runs `seconds` of the session, calling `before_tick` with the tick index, and returns
// the bytes the client sent the server in each second.
std::vector<uint64_t> UplinkBytesPerSecond(
    InMemoryNetwork& network, Z13TestWorld& server, Z13TestWorld& client, uint64_t seconds,
    const std::function<void(uint64_t)>& before_tick) {
  std::vector<uint64_t> per_second;
  uint64_t second_start = network.TrafficTo(kDefaultServerPort).bytes;
  for (uint64_t tick = 0; tick < seconds * kTicksPerSecond; ++tick) {
    before_tick(tick);
    RunNetworkUntil(network, {server, client}, kNetTestDeltaTime, 1, [] { return false; });
    if ((tick + 1) % kTicksPerSecond == 0) {
      const uint64_t now = network.TrafficTo(kDefaultServerPort).bytes;
      per_second.push_back(now - second_start);
      second_start = now;
    }
  }
  return per_second;
}

// A held key adds a batch per send interval.
class UplinkBudgetTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(RunNetworkUntil(
        *network_, {server_, client_}, kNetTestDeltaTime, kMaxNetTestTicks, [&] { return IsConnected(client_); }));
  }

  std::shared_ptr<InMemoryNetwork> network_ = std::make_shared<InMemoryNetwork>();
  Z13TestWorld server_ = MakeServer(network_);
  Z13TestWorld client_ = MakeClient(network_);
};

// W for 5 s, then 2 s of mouse look changing every tick (a steady delta would be sent once).
TEST_F(UplinkBudgetTest, ScriptedSessionStaysWithinBudget) {
  const auto per_second = UplinkBytesPerSecond(*network_, server_, client_, kSessionSeconds, [&](uint64_t tick) {
    if (tick == 0) {
      client_.EmitInput(KeyDown(Keycode::KEY_W));
    } else if (tick == kHoldForwardTicks) {
      client_.EmitInput(KeyUp(Keycode::KEY_W));
    }
    if (tick >= kHoldForwardTicks && tick < kHoldForwardTicks + kMouseLookTicks) {
      z13::input::MouseMoveEvent look;
      look.delta = {.x = kMouseDeltaX + static_cast<int>(tick % kMouseJitterX),
                    .y = static_cast<int>(tick % kMouseJitterY) - 1};
      client_.EmitInput(look);
    }
  });

  const uint64_t peak = std::ranges::max(per_second);
  EXPECT_GT(peak, kIdleUplinkBudgetBytesPerSecond) << "the mouse look never reached the wire";
  EXPECT_LE(peak, kClientUplinkBudgetBytesPerSecond);
}

TEST_F(UplinkBudgetTest, IdleClientStaysWithinIdleBudget) {
  const auto per_second = UplinkBytesPerSecond(*network_, server_, client_, kIdleSeconds, [](uint64_t) {});

  EXPECT_LE(std::ranges::max(per_second), kIdleUplinkBudgetBytesPerSecond);
}

}  // namespace
}  // namespace z13::net
