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

// A bench, not a test (docs/network-scaling-plan.md, "f/net-residual-measure"): how long a tick's relayed commands
// take to arrive over real ENet when datagrams are lost, one packed message against one message per player.
//   z13_test_runner --gtest_also_run_disabled_tests --gtest_filter='EnetLossBench.*'

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <format>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <net_module/enet_transport.h>

#include "lossy_udp_proxy.h"

namespace z13::net {
namespace {

using Clock = std::chrono::steady_clock;

constexpr uint16_t kServerPort = 27101;
constexpr auto kOneWayDelay = std::chrono::milliseconds(25);
constexpr auto kSendInterval = std::chrono::milliseconds(50);  // send_interval_ticks 3 at 60 Hz
constexpr auto kRunTime = std::chrono::seconds(10);
constexpr auto kDrainTime = std::chrono::seconds(5);
constexpr auto kConnectTime = std::chrono::seconds(10);
constexpr auto kPumpInterval = std::chrono::milliseconds(1);
constexpr size_t kRunBytes = 56;       // one player's run in a packed message (measured)
constexpr size_t kMessageBytes = 104;  // the same run in a message of its own (measured)
constexpr size_t kHeaderBytes = 8 + 4;  // send time and tick, at the start of every message
constexpr std::array kRepeats = {1u, 2u};
constexpr std::array kPlayers = {31, 199};
constexpr std::array kLosses = {0., 0.02, 0.05};
constexpr double kMedian = 0.5;
constexpr double kP95 = 0.95;
constexpr double kP99 = 0.99;

enum class Shape { kOneMessagePerPlayer, kPacked };

std::string_view NameOf(Shape shape) {
  return shape == Shape::kPacked ? "packed" : "per player";
}

std::vector<std::byte> MakeMessage(uint32_t tick, Clock::time_point sent, size_t size) {
  std::vector<std::byte> bytes(size);
  const int64_t sent_ns = sent.time_since_epoch().count();
  std::memcpy(bytes.data(), &sent_ns, sizeof sent_ns);
  std::memcpy(bytes.data() + sizeof sent_ns, &tick, sizeof tick);
  return bytes;
}

double Percentile(const std::vector<double>& sorted, double fraction) {
  if (sorted.empty()) {
    return 0.;
  }
  const auto index = static_cast<size_t>(fraction * static_cast<double>(sorted.size()));
  return sorted[std::clamp(index, size_t {}, sorted.size() - 1)];
}

struct Latencies {
  std::vector<double> ms;  // per tick whose messages all arrived
  size_t sent {};
  uint64_t dropped {};
};

Latencies MeasureLatency(Shape shape, int players, double loss, uint32_t seed) {
  Latencies result;
  auto server = CreateEnetServerTransport(kServerPort);
  auto proxy = LossyUdpProxy::Create({.server = {.host = "127.0.0.1", .port = kServerPort},
                                      .loss = loss,
                                      .one_way_delay = kOneWayDelay,
                                      .seed = seed});
  if (!server || !proxy) {
    ADD_FAILURE() << "can't set up the link";
    return result;
  }
  auto client = CreateEnetClientTransport({.host = "127.0.0.1", .port = (*proxy)->Port()});
  if (!client) {
    ADD_FAILURE() << "can't set up the link";
    return result;
  }
  std::optional<ConnectionId> to_client;
  bool client_connected = false;
  const auto pump = [&] {
    (*proxy)->Pump();
    for (const TransportEvent& event : (*server)->Service()) {
      if (event.kind == TransportEventKind::kConnected) {
        to_client = event.connection;
      }
    }
    return (*client)->Service();
  };
  const auto connect_deadline = Clock::now() + kConnectTime;
  while (!(to_client && client_connected) && Clock::now() < connect_deadline) {
    for (const TransportEvent& event : pump()) {
      client_connected = client_connected || event.kind == TransportEventKind::kConnected;
    }
    std::this_thread::sleep_for(kPumpInterval);
  }
  if (!to_client || !client_connected) {
    ADD_FAILURE() << "didn't connect";
    return result;
  }

  const size_t per_tick = shape == Shape::kPacked ? 1 : static_cast<size_t>(players);
  const size_t size = shape == Shape::kPacked ? kRunBytes * static_cast<size_t>(players) : kMessageBytes;
  std::vector<size_t> arrived;
  const auto start = Clock::now();
  auto next_send = start;
  uint32_t tick = 0;
  for (auto now = start; now < start + kRunTime + kDrainTime; now = Clock::now()) {
    if (now >= next_send && now < start + kRunTime) {
      for (size_t i = 0; i < per_tick; ++i) {
        (*server)->Send(*to_client, Channel::kReliable, MakeMessage(tick, now, size));
      }
      arrived.push_back(0);
      ++tick;
      next_send += kSendInterval;
    }
    for (const TransportEvent& event : pump()) {
      if (event.kind != TransportEventKind::kReceived || event.data.size() < kHeaderBytes) {
        continue;
      }
      int64_t sent_ns = 0;
      uint32_t received_tick = 0;
      std::memcpy(&sent_ns, event.data.data(), sizeof sent_ns);
      std::memcpy(&received_tick, event.data.data() + sizeof sent_ns, sizeof received_tick);
      if (received_tick < arrived.size() && ++arrived[received_tick] == per_tick) {
        const auto sent = Clock::time_point(Clock::duration(sent_ns));
        result.ms.push_back(std::chrono::duration<double, std::milli>(Clock::now() - sent).count());
      }
    }
    std::this_thread::sleep_for(kPumpInterval);
  }
  result.sent = arrived.size();
  result.dropped = (*proxy)->Dropped();
  return result;
}

TEST(EnetLossBench, DISABLED_PackedAgainstOneMessagePerPlayer) {
  std::cout << "players | shape      | loss | ticks arrived / sent | latency ms p50 / p95 / p99 / max | datagrams dropped\n";
  for (const int players : kPlayers) {
    for (const double loss : kLosses) {
      for (const Shape shape : {Shape::kOneMessagePerPlayer, Shape::kPacked}) {
        Latencies pooled;
        for (const uint32_t seed : kRepeats) {
          const Latencies run = MeasureLatency(shape, players, loss, seed);
          pooled.ms.insert(pooled.ms.end(), run.ms.begin(), run.ms.end());
          pooled.sent += run.sent;
          pooled.dropped += run.dropped;
        }
        std::ranges::sort(pooled.ms);
        std::cout << std::format(
            "{:7} | {:10} | {:3.0f}% | {:9} / {:6} | {:6.0f} / {:6.0f} / {:6.0f} / {:6.0f} | {}\n", players, NameOf(shape),
            loss * 100, pooled.ms.size(), pooled.sent, Percentile(pooled.ms, kMedian), Percentile(pooled.ms, kP95),
            Percentile(pooled.ms, kP99), pooled.ms.empty() ? 0. : pooled.ms.back(), pooled.dropped);
      }
    }
  }
}

}  // namespace
}  // namespace z13::net
