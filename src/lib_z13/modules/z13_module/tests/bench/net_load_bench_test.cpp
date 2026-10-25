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

// A bench, not a test (see docs/network-scaling-plan.md, "f/net-load-metrics"). Run it with
//   z13_test_runner --gtest_also_run_disabled_tests --gtest_filter='NetLoadBench.*'

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <format>
#include <functional>
#include <iostream>
#include <ranges>
#include <string>
#include <vector>

#include <lib_core/settings/config.h>

#include "../../../net_module/src/net_session.h"
#include "../support/load_session.h"

namespace z13::net {
namespace {

using z13::testing::LoadSession;

constexpr std::array kClientCounts = {1, 2, 4, 8, 16, 32};
constexpr uint64_t kConnectTicks = 3000;
constexpr uint64_t kWarmUpTicks = 120;
constexpr uint64_t kMeasuredTicks = 300;
constexpr double kTicksPerSecond = 60.;
constexpr size_t kTopMessages = 3;

struct Snapshot {
  SentTraffic server;
  uint64_t clients_bytes {};
  std::chrono::nanoseconds server_time {};
};

Snapshot Take(LoadSession& session) {
  Snapshot snapshot {.server = session.Server().World().get<NetSession>().Sent(), .server_time = session.ServerTime()};
  for (size_t i = 0; i < session.ClientCount(); ++i) {
    snapshot.clients_bytes += session.Client(i).World().get<NetSession>().Sent().Total().bytes;
  }
  return snapshot;
}

uint64_t ChannelBytes(const SentTraffic& sent, Channel channel) {
  uint64_t bytes = 0;
  for (const auto& [link, stats] : sent.by_link) {
    bytes += link.channel == channel ? stats.bytes : 0;
  }
  return bytes;
}

std::string TopMessages(const SentTraffic& before, const SentTraffic& after, double seconds) {
  std::vector<std::pair<uint64_t, fbs::net::MessageBody>> grown;
  for (const auto& [message, stats] : after.by_message) {
    const auto was = before.by_message.find(message);
    grown.emplace_back(stats.bytes - (was == before.by_message.end() ? 0 : was->second.bytes), message);
  }
  std::ranges::sort(grown, std::greater<> {});
  std::string text;
  for (const auto& [bytes, message] : grown | std::views::take(kTopMessages)) {
    text += std::format(" {} {:.0f}", fbs::net::EnumNameMessageBody(message), static_cast<double>(bytes) / seconds);
  }
  return text;
}

TEST(NetLoadBench, DISABLED_ServerLoadByClientCount) {
  std::cout << "clients | server ms/tick | server B/s (per client) | reliable / unreliable / hints B/s | client uplink B/s"
               " | top messages B/s\n";
  for (const int clients : kClientCounts) {
    LoadSession session(static_cast<size_t>(clients));
    ASSERT_TRUE(session.Connect(kConnectTicks)) << clients << " clients didn't connect";
    for (uint64_t tick = 0; tick < kWarmUpTicks; ++tick) {
      session.Tick();
    }
    const Snapshot before = Take(session);
    for (uint64_t tick = 0; tick < kMeasuredTicks; ++tick) {
      session.Tick();
    }
    const Snapshot after = Take(session);

    const double seconds = static_cast<double>(kMeasuredTicks) / kTicksPerSecond;
    const double server_ms =
        std::chrono::duration<double, std::milli>(after.server_time - before.server_time).count() / kMeasuredTicks;
    const auto per_second = [&](Channel channel) {
      return static_cast<double>(ChannelBytes(after.server, channel) - ChannelBytes(before.server, channel)) / seconds;
    };
    const double server_bytes = static_cast<double>(after.server.Total().bytes - before.server.Total().bytes) / seconds;
    std::cout << std::format(
        "{:7} | {:14.2f} | {:10.0f} ({:7.0f}) | {:9.0f} / {:9.0f} / {:6.0f} | {:17.0f} |{}\n", clients, server_ms,
        server_bytes, server_bytes / clients, per_second(Channel::kReliable), per_second(Channel::kUnreliable),
        per_second(Channel::kHints), static_cast<double>(after.clients_bytes - before.clients_bytes) / seconds / clients,
        TopMessages(before.server, after.server, seconds));
  }
}

}  // namespace
}  // namespace z13::net
