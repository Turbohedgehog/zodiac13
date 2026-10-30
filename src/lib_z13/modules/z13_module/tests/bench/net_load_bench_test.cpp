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
#include <string_view>
#include <string>
#include <vector>

#include <Eigen/Dense>

#include <lib_core/settings/config.h>
#include <lib_core/state/rollback.h>
#include <lib_core/state/world_serializer.h>
#include <lib_core/utils/math.h>
#include <lib_core/utils/system_times.h>

#include <net_module/state_digest.h>
#include <z13/components/player.h>
#include <z13_settings/settings.h>
#include <z13_tests/shipped_station.h>

#include "../../../net_module/src/net_session.h"
#include "../support/load_session.h"

namespace z13::net {
namespace {

using z13::testing::LoadSession;
using z13::testing::Steering;

constexpr std::array kClientCounts = {1, 2, 4, 8, 16, 32};
constexpr uint64_t kConnectTicks = 3000;
constexpr uint64_t kWarmUpTicks = 120;
constexpr uint64_t kMeasuredTicks = 300;
constexpr double kTicksPerSecond = 60.;
constexpr size_t kTopMessages = 3;
constexpr size_t kAllMessages = 20;
constexpr size_t kTopSystems = 6;

struct Snapshot {
  SentTraffic server;
  uint64_t clients_bytes {};
  std::chrono::nanoseconds server_time {};
  std::vector<z13::SystemTime> systems;
  z13::flecs_tools::RollbackMetrics rollbacks;
};

Snapshot Take(LoadSession& session) {
  flecs::world server = session.Server().World();
  Snapshot snapshot {.server = server.get<NetSession>().Sent(),
                     .server_time = session.ServerTime(),
                     .systems = z13::SystemTimes(server),
                     .rollbacks = server.get<z13::flecs_tools::RollbackMetrics>()};
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

uint64_t MessageBytes(const SentTraffic& sent, fbs::net::MessageBody message) {
  const auto found = sent.by_message.find(message);
  return found == sent.by_message.end() ? uint64_t {} : found->second.bytes;
}

uint64_t ResyncsOfClients(LoadSession& session) {
  uint64_t resyncs = 0;
  for (size_t i = 0; i < session.ClientCount(); ++i) {
    resyncs += session.Client(i).World().get<StateDigests>().resyncs;
  }
  return resyncs;
}

std::string TopMessages(
    const SentTraffic& before, const SentTraffic& after, double seconds, size_t count = kTopMessages) {
  std::vector<std::pair<uint64_t, fbs::net::MessageBody>> grown;
  for (const auto& [message, stats] : after.by_message) {
    const auto was = before.by_message.find(message);
    grown.emplace_back(stats.bytes - (was == before.by_message.end() ? 0 : was->second.bytes), message);
  }
  std::ranges::sort(grown, std::greater<> {});
  std::string text;
  for (const auto& [bytes, message] : grown | std::views::take(count)) {
    text += std::format(" {} {:.0f}", fbs::net::EnumNameMessageBody(message), static_cast<double>(bytes) / seconds);
  }
  return text;
}

std::string MessageRate(
    const SentTraffic& before, const SentTraffic& after, fbs::net::MessageBody message, double seconds) {
  const auto total = [&](const SentTraffic& sent) {
    const auto found = sent.by_message.find(message);
    return found == sent.by_message.end() ? TrafficStats {} : found->second;
  };
  const uint64_t packets = total(after).packets - total(before).packets;
  const uint64_t bytes = total(after).bytes - total(before).bytes;
  return std::format(
      " {} {:.0f} packets/s, {:.0f} B/packet", fbs::net::EnumNameMessageBody(message),
      static_cast<double>(packets) / seconds,
      packets > 0 ? static_cast<double>(bytes) / static_cast<double>(packets) : 0.);
}

std::string TopSystems(const std::vector<z13::SystemTime>& before, const std::vector<z13::SystemTime>& after) {
  const std::vector<z13::SystemTime> spent = z13::SystemTimesPerFrame(before, after, kMeasuredTicks);
  std::string text = std::format(" all {:.2f} |", std::ranges::fold_left(spent | std::views::transform(&z13::SystemTime::ms), 0., std::plus<> {}));
  for (const z13::SystemTime& system : spent | std::views::take(kTopSystems)) {
    text += std::format(" {} {:.2f}", system.path, system.ms);
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
    ecs_measure_system_time(session.Server().World(), true);
    const Snapshot before = Take(session);
    for (uint64_t tick = 0; tick < kMeasuredTicks; ++tick) {
      session.Tick();
    }
    const Snapshot after = Take(session);
    ecs_measure_system_time(session.Server().World(), false);

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
    std::cout << std::format(
        "        server: {} rollbacks, {} ticks replayed; ms/tick by system:{}\n",
        after.rollbacks.rollbacks - before.rollbacks.rollbacks,
        after.rollbacks.replayed_ticks - before.rollbacks.replayed_ticks, TopSystems(before.systems, after.systems));
    std::cout << std::format(
        "        server sends:{}\n",
        MessageRate(before.server, after.server, fbs::net::MessageBody::SequencedCommands, seconds));
  }
}

TEST(NetLoadBench, DISABLED_RollbackCostByClientCount) {
  constexpr int kRepeats = 50;
  std::cout << "clients | capture ms | restore ms | state entities\n";
  for (const int clients : kClientCounts) {
    LoadSession session(static_cast<size_t>(clients));
    ASSERT_TRUE(session.Connect(kConnectTicks)) << clients << " clients didn't connect";
    for (uint64_t tick = 0; tick < kWarmUpTicks; ++tick) {
      session.Tick();
    }
    flecs::world server = session.Server().World();
    const auto snapshot = z13::flecs_tools::CaptureState(server);
    ASSERT_TRUE(snapshot.has_value());
    const auto start = std::chrono::steady_clock::now();
    for (int repeat = 0; repeat < kRepeats; ++repeat) {
      ASSERT_TRUE(z13::flecs_tools::CaptureState(server).has_value());
    }
    const auto captured = std::chrono::steady_clock::now();
    for (int repeat = 0; repeat < kRepeats; ++repeat) {
      ASSERT_TRUE(z13::flecs_tools::RestoreWorld(server, *snapshot).has_value());
    }
    const auto restored = std::chrono::steady_clock::now();
    const auto per_repeat = [](auto from, auto to) {
      return std::chrono::duration<double, std::milli>(to - from).count() / kRepeats;
    };
    std::cout << std::format(
        "{:7} | {:10.2f} | {:10.2f} | {}\n", clients, per_repeat(start, captured), per_repeat(captured, restored),
        snapshot->entities.size());
  }
}

TEST(NetLoadBench, DISABLED_RollbacksBySteering) {
  struct Case {
    std::string_view name;
    Steering steering;
  };
  constexpr std::array cases = {
      Case {"idle", {.walk = false, .look = false}}, Case {"walk", {.walk = true, .look = false}},
      Case {"look", {.walk = false, .look = true}}, Case {"walk+look", {.walk = true, .look = true}}};
  for (const int clients : {1, 4}) {
    for (const Case& test_case : cases) {
      LoadSession session(static_cast<size_t>(clients), test_case.steering);
      ASSERT_TRUE(session.Connect(kConnectTicks));
      for (uint64_t tick = 0; tick < kWarmUpTicks; ++tick) {
        session.Tick();
      }
      const Snapshot before = Take(session);
      for (uint64_t tick = 0; tick < kMeasuredTicks; ++tick) {
        session.Tick();
      }
      const Snapshot after = Take(session);
      std::cout << std::format(
          "{} clients, {:9}: {} rollbacks, {} ticks replayed, server {:.2f} ms/tick\n", clients, test_case.name,
          after.rollbacks.rollbacks - before.rollbacks.rollbacks,
          after.rollbacks.replayed_ticks - before.rollbacks.replayed_ticks,
          std::chrono::duration<double, std::milli>(after.server_time - before.server_time).count() / kMeasuredTicks);
    }
  }
}

TEST(NetLoadBench, DISABLED_RollbacksByTuning) {
  constexpr int kClients = 16;
  for (const uint64_t send_interval : {3, 6}) {
    for (const uint64_t rollback_delay : {6, 12, 24}) {
      LoadSession session(kClients, {}, [&](z13::Settings& settings) {
        settings.net->send_interval_ticks = send_interval;
        settings.core->max_rollback_delay_ticks = rollback_delay;
      });
      ASSERT_TRUE(session.Connect(kConnectTicks));
      for (uint64_t tick = 0; tick < kWarmUpTicks; ++tick) {
        session.Tick();
      }
      const Snapshot before = Take(session);
      for (uint64_t tick = 0; tick < kMeasuredTicks; ++tick) {
        session.Tick();
      }
      const Snapshot after = Take(session);
      std::cout << std::format(
          "{} clients, send every {} ticks, rollback delay {:2}: {} rollbacks, {} ticks replayed, server {:.2f} ms/tick\n",
          kClients, send_interval, rollback_delay, after.rollbacks.rollbacks - before.rollbacks.rollbacks,
          after.rollbacks.replayed_ticks - before.rollbacks.replayed_ticks,
          std::chrono::duration<double, std::milli>(after.server_time - before.server_time).count() / kMeasuredTicks);
    }
  }
}

TEST(NetLoadBench, DISABLED_RollbacksByClockLead) {
  constexpr int kClients = 8;
  constexpr z13::net::FaultConfig kLatency {.min_delay_ticks = 1, .max_delay_ticks = 2};
  for (const uint64_t lead : {0, 2, 3, 4, 6, 8}) {
    LoadSession session(kClients, {}, [lead](z13::Settings& settings) { settings.net->clock_lead_ticks = lead; });
    session.Network().SetFaultConfig(kLatency);
    ASSERT_TRUE(session.Connect(kConnectTicks));
    for (uint64_t tick = 0; tick < kWarmUpTicks; ++tick) {
      session.Tick();
    }
    const Snapshot before = Take(session);
    for (uint64_t tick = 0; tick < kMeasuredTicks; ++tick) {
      session.Tick();
    }
    const Snapshot after = Take(session);
    std::cout << std::format(
        "{} clients, one-way {}-{} ticks, clock lead {}: {} rollbacks, {} ticks replayed, server {:.2f} ms/tick\n",
        kClients, kLatency.min_delay_ticks, kLatency.max_delay_ticks, lead,
        after.rollbacks.rollbacks - before.rollbacks.rollbacks,
        after.rollbacks.replayed_ticks - before.rollbacks.replayed_ticks,
        std::chrono::duration<double, std::milli>(after.server_time - before.server_time).count() / kMeasuredTicks);
  }
}

// Players walking apart: what interest saves in traffic and server time, and what it costs in resyncs.
TEST(NetLoadBench, DISABLED_InterestByClientCount) {
  constexpr uint64_t kSpreadTicks = 900;
  constexpr std::array kInterestClientCounts = {16};
  std::cout << "clients | interest | server ms/tick | server B/s per client | SequencedCommands B/s per client"
               " | resyncs | spread m\n";
  for (const int clients : kInterestClientCounts) {
    for (const bool interest : {false, true}) {
      LoadSession session(
          static_cast<size_t>(clients), {.walk = true, .look = false, .fan_out = true},
          [interest](z13::Settings& settings) { settings.net->interest_events = interest; });
      ASSERT_TRUE(session.Connect(kConnectTicks)) << clients << " clients didn't connect";
      for (uint64_t tick = 0; tick < kSpreadTicks; ++tick) {
        session.Tick();
      }
      ecs_measure_system_time(session.Server().World(), true);
      const Snapshot before = Take(session);
      for (uint64_t tick = 0; tick < kMeasuredTicks; ++tick) {
        session.Tick();
      }
      const Snapshot after = Take(session);
      ecs_measure_system_time(session.Server().World(), false);

      const double seconds = static_cast<double>(kMeasuredTicks) / kTicksPerSecond;
      const auto commands = [](const SentTraffic& sent) {
        const auto found = sent.by_message.find(fbs::net::MessageBody::SequencedCommands);
        return found == sent.by_message.end() ? uint64_t {} : found->second.bytes;
      };
      uint64_t resyncs = 0;
      for (size_t i = 0; i < session.ClientCount(); ++i) {
        resyncs += session.Client(i).World().get<StateDigests>().resyncs;
      }
      Eigen::Vector3f centroid = Eigen::Vector3f::Zero();
      std::vector<Eigen::Vector3f> positions;
      session.Server().World().query<const z13::gameplay::Player, const Eigen::Matrix4f>().each(
          [&](const z13::gameplay::Player&, const Eigen::Matrix4f& transform) {
            positions.push_back(z13::math::ExtractTranslation<float>(transform));
            centroid += positions.back();
          });
      centroid /= static_cast<float>(std::max<size_t>(positions.size(), 1));
      double spread = 0.;
      for (const Eigen::Vector3f& position : positions) {
        spread += (position - centroid).norm() / static_cast<double>(std::max<size_t>(positions.size(), 1));
      }
      const double server_bytes = static_cast<double>(after.server.Total().bytes - before.server.Total().bytes);
      std::cout << std::format(
          "{:7} | {:8} | {:14.2f} | {:21.0f} | {:32.0f} | {:7} | {:8.1f}\n", clients, interest ? "on" : "off",
          std::chrono::duration<double, std::milli>(after.server_time - before.server_time).count() / kMeasuredTicks,
          server_bytes / seconds / clients,
          static_cast<double>(commands(after.server) - commands(before.server)) / seconds / clients, resyncs, spread);
      std::cout << std::format("        ms/tick by system:{}\n", TopSystems(before.systems, after.systems));
      std::cout << std::format(
          "        server B/s per client by message:{}\n",
          TopMessages(before.server, after.server, seconds * clients, kAllMessages));
      for (const auto message : {fbs::net::MessageBody::SectorEntered, fbs::net::MessageBody::SectorLeft}) {
        std::cout << std::format(
            "        server, all clients together:{}\n", MessageRate(before.server, after.server, message, seconds));
      }
    }
  }
}

// Everybody in one hall of the `test` station: what ghosts save when a client simulates only the K nearest players.
TEST(NetLoadBench, DISABLED_HallByClientCount) {
  constexpr std::array kHallClientCounts = {8, 16, 32};
  constexpr size_t kSimulatedPlayers = 8;
  constexpr size_t kSimulatedSlack = 2;
  constexpr double kWholeStationM = 5000.;
  std::cout << "clients | ghosts | server ms/tick | server B/s per client | SequencedCommands B/s | poses B/s | resyncs"
               " | rollbacks (replayed ticks)\n";
  for (const int clients : kHallClientCounts) {
    for (const bool ghosts : {false, true}) {
      LoadSession session(
          static_cast<size_t>(clients), {}, [ghosts](z13::Settings& settings) {
            settings.net->interest_events = true;
            settings.net->ghosts = ghosts;
            settings.net->ghost_radius_m = kWholeStationM;
            settings.net->simulated_radius_m = kWholeStationM;
            settings.net->simulated_players_max = kSimulatedPlayers;
            settings.net->simulated_players_slack = kSimulatedSlack;
          },
          z13::testing::kTestScene);
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
      const uint64_t resyncs = ResyncsOfClients(session);
      const double per_client = seconds * clients;
      const uint64_t poses_after = ChannelBytes(after.server, Channel::kPoses);
      const double pose_bytes = static_cast<double>(poses_after - ChannelBytes(before.server, Channel::kPoses));
      std::cout << std::format(
          "{:7} | {:6} | {:14.2f} | {:21.0f} | {:21.0f} | {:9.0f} | {:7} | {} ({})\n", clients, ghosts ? "on" : "off",
          std::chrono::duration<double, std::milli>(after.server_time - before.server_time).count() / kMeasuredTicks,
          static_cast<double>(after.server.Total().bytes - before.server.Total().bytes) / per_client,
          static_cast<double>(MessageBytes(after.server, fbs::net::MessageBody::SequencedCommands) -
                              MessageBytes(before.server, fbs::net::MessageBody::SequencedCommands)) / per_client,
          pose_bytes / per_client,
          resyncs, after.rollbacks.rollbacks - before.rollbacks.rollbacks,
          after.rollbacks.replayed_ticks - before.rollbacks.replayed_ticks);
    }
  }
}

// Everybody in one hall of the `test` station, ghosts on, the budget K swept: what one more simulated player costs a
// client against one more ghost (docs/network-scaling-plan.md, 5b).
TEST(NetLoadBench, DISABLED_HallByBudget) {
  constexpr int kClients = 32;
  constexpr std::array kBudgets = {2, 4, 8, 12, 16, 24, 32};
  constexpr double kWholeStationM = 5000.;
  std::cout << "K | server ms/tick | server B/s per client | SequencedCommands B/s | poses B/s | resyncs | rollbacks "
               "(replayed ticks)\n";
  for (const int budget : kBudgets) {
    LoadSession session(static_cast<size_t>(kClients), {}, [budget](z13::Settings& settings) {
      settings.net->interest_events = true;
      settings.net->ghosts = true;
      settings.net->ghost_radius_m = kWholeStationM;
      settings.net->simulated_radius_m = kWholeStationM;
      settings.net->simulated_players_max = static_cast<uint64_t>(budget);
    }, z13::testing::kTestScene);
    ASSERT_TRUE(session.Connect(kConnectTicks)) << "K = " << budget << ": the clients didn't connect";
    for (uint64_t tick = 0; tick < kWarmUpTicks; ++tick) {
      session.Tick();
    }
    const Snapshot before = Take(session);
    for (uint64_t tick = 0; tick < kMeasuredTicks; ++tick) {
      session.Tick();
    }
    const Snapshot after = Take(session);

    const double per_client = static_cast<double>(kMeasuredTicks) / kTicksPerSecond * kClients;
    const uint64_t resyncs = ResyncsOfClients(session);
    std::cout << std::format(
        "{:2} | {:14.2f} | {:21.0f} | {:21.0f} | {:9.0f} | {:7} | {} ({})\n", budget,
        std::chrono::duration<double, std::milli>(after.server_time - before.server_time).count() / kMeasuredTicks,
        static_cast<double>(after.server.Total().bytes - before.server.Total().bytes) / per_client,
        static_cast<double>(MessageBytes(after.server, fbs::net::MessageBody::SequencedCommands) -
                            MessageBytes(before.server, fbs::net::MessageBody::SequencedCommands)) / per_client,
        static_cast<double>(ChannelBytes(after.server, Channel::kPoses) - ChannelBytes(before.server, Channel::kPoses)) /
            per_client,
        resyncs, after.rollbacks.rollbacks - before.rollbacks.rollbacks,
        after.rollbacks.replayed_ticks - before.rollbacks.replayed_ticks);
    const auto channel_rate = [&](Channel channel) {
      return static_cast<double>(ChannelBytes(after.server, channel) - ChannelBytes(before.server, channel)) /
          per_client;
    };
    std::cout << std::format(
        "   reliable / unreliable / hints / poses B/s per client: {:.0f} / {:.0f} / {:.0f} / {:.0f}\n   by message:{}\n",
        channel_rate(Channel::kReliable), channel_rate(Channel::kUnreliable), channel_rate(Channel::kHints),
        channel_rate(Channel::kPoses), TopMessages(before.server, after.server, per_client, kAllMessages));
  }
}

// Light clients (z13::net::LightClient) next to a few full ones, whose resyncs say whether real clients keep up.
constexpr size_t kFullClients = 4;
constexpr std::array kLightTotals = {32, 64, 128, 200};

void PrintLightHeader() {
  std::cout << "clients | server ms/tick | server B/s per client | SequencedCommands B/s | hints B/s | poses B/s"
               " | full client resyncs | rollbacks (replayed ticks)\n";
}

// Every client, light or full, counts in the per-client rates.
void MeasureLight(LoadSession& session, const std::string& label) {
  ASSERT_TRUE(session.Connect(kConnectTicks)) << label << ": the clients didn't connect";
  for (uint64_t tick = 0; tick < kWarmUpTicks; ++tick) {
    session.Tick();
  }
  ecs_measure_system_time(session.Server().World(), true);
  const Snapshot before = Take(session);
  for (uint64_t tick = 0; tick < kMeasuredTicks; ++tick) {
    session.Tick();
  }
  const Snapshot after = Take(session);
  ecs_measure_system_time(session.Server().World(), false);
  const double per_client = static_cast<double>(kMeasuredTicks) / kTicksPerSecond *
      static_cast<double>(session.ClientCount() + session.LightClientCount());
  const auto channel_rate = [&](Channel channel) {
    return static_cast<double>(ChannelBytes(after.server, channel) - ChannelBytes(before.server, channel)) / per_client;
  };
  std::cout << std::format(
      "{:>7} | {:14.2f} | {:21.0f} | {:21.0f} | {:9.0f} | {:9.0f} | {:19} | {} ({})\n", label,
      std::chrono::duration<double, std::milli>(after.server_time - before.server_time).count() / kMeasuredTicks,
      static_cast<double>(after.server.Total().bytes - before.server.Total().bytes) / per_client,
      static_cast<double>(MessageBytes(after.server, fbs::net::MessageBody::SequencedCommands) -
                          MessageBytes(before.server, fbs::net::MessageBody::SequencedCommands)) / per_client,
      channel_rate(Channel::kHints), channel_rate(Channel::kPoses), ResyncsOfClients(session),
      after.rollbacks.rollbacks - before.rollbacks.rollbacks,
      after.rollbacks.replayed_ticks - before.rollbacks.replayed_ticks);
  std::cout << std::format(
      "        by message:{}\n", TopMessages(before.server, after.server, per_client, kAllMessages));
  std::cout << std::format("        ms/tick by system:{}\n", TopSystems(before.systems, after.systems));
}

// Everybody in one hall of the `test` station, ghosts on with the default budget: the worst case of 200 players.
TEST(NetLoadBench, DISABLED_LightHallByClientCount) {
  constexpr double kWholeStationM = 5000.;
  PrintLightHeader();
  for (const int total : kLightTotals) {
    LoadSession session(kFullClients, {}, [](z13::Settings& settings) {
      settings.net->interest_events = true;
      settings.net->ghost_radius_m = kWholeStationM;
      settings.net->simulated_radius_m = kWholeStationM;
    }, z13::testing::kTestScene);
    session.AddLightClients(static_cast<size_t>(total) - kFullClients);
    MeasureLight(session, std::to_string(total));
  }
}

// Players spread over the spawn points of `octant` and walking off in their own directions, default radii: the
// realistic case.
TEST(NetLoadBench, DISABLED_LightSpreadByClientCount) {
  PrintLightHeader();
  for (const int total : kLightTotals) {
    LoadSession session(
        kFullClients, {.look = false, .fan_out = true},
        [](z13::Settings& settings) { settings.net->interest_events = true; }, z13::testing::kOctantScene);
    session.AddLightClients(static_cast<size_t>(total) - kFullClients);
    MeasureLight(session, std::to_string(total));
  }
}

// 200 players in one hall of the `test` station, the budget K swept.
TEST(NetLoadBench, DISABLED_LightHallByBudget) {
  constexpr size_t kClients = 200;
  constexpr std::array kBudgets = {4, 8, 12, 16};
  constexpr double kWholeStationM = 5000.;
  PrintLightHeader();
  for (const int budget : kBudgets) {
    LoadSession session(kFullClients, {}, [budget](z13::Settings& settings) {
      settings.net->interest_events = true;
      settings.net->ghost_radius_m = kWholeStationM;
      settings.net->simulated_radius_m = kWholeStationM;
      settings.net->simulated_players_max = static_cast<uint64_t>(budget);
    }, z13::testing::kTestScene);
    session.AddLightClients(kClients - kFullClients);
    MeasureLight(session, std::format("K={}", budget));
  }
}

}  // namespace
}  // namespace z13::net
