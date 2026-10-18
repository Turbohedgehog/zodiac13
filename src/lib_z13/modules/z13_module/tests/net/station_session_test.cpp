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
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <ranges>
#include <string>
#include <vector>

#include <Eigen/Dense>

#include <lib_core/state/rollback.h>
#include <lib_core/state/world_json_store.h>
#include <lib_core/state/world_snapshot_history.h>
#include <lib_core/utils/file_io.h>
#include <lib_core/time/simulation_clock.h>
#include <lib_core/utils/math.h>
#include <lib_core/utils/system_times.h>

#include <net_module/divergence_dump.h>
#include <net_module/in_memory_transport.h>
#include <net_module/state_digest.h>

#include <z13/components/gameplay.h>
#include <z13/components/gravity.h>
#include <z13/components/inventory.h>
#include <z13/components/player.h>
#include <z13/components/player_action.h>
#include <z13/components/player_parts.h>
#include <z13/components/net.h>
#include <z13_module/gameplay/gameplay_entities.h>

#include <z13_tests/shipped_station.h>

#include "../support/building_test_helpers.h"
#include "../support/test_network.h"
#include "../support/z13_test_world.h"

// A client joining a host on the largest shipped station and playing there. Opt-in, it takes
// tens of seconds in Debug: --gtest_also_run_disabled_tests --gtest_filter=StationSessionTest.*
namespace z13::net {
namespace {

using z13::testing::KeyDown;
using z13::testing::KeyUp;
using z13::testing::kConnectArg;
using z13::testing::kNetTestDeltaTime;
using z13::testing::kServerArg;
using z13::testing::kStationSceneArg;
using z13::testing::kTestServerEndpoint;
using z13::testing::RunNetworkUntil;
using z13::testing::Z13TestWorld;
using Keycode = z13::fbs::input::Keycode;

constexpr uint32_t kClientId = 1;
constexpr uint64_t kMaxJoinTicks = 1200;
// One second of each action at 60 Hz.
constexpr uint64_t kActionTicks = 60;
constexpr uint64_t kSettleTicks = 30;
constexpr uint64_t kDigestChecks = 3;

class StationSessionTest : public ::testing::Test {
 protected:
  bool Run(uint64_t max_ticks, const std::function<bool()>& condition) {
    return RunNetworkUntil(*network_, {server_, client_}, kNetTestDeltaTime, max_ticks, condition);
  }

  void RunTicks(uint64_t ticks) {
    Run(ticks, [] { return false; });
  }

  void Hold(Keycode key) {
    client_.EmitInput(KeyDown(key));
    RunTicks(kActionTicks);
    client_.EmitInput(KeyUp(key));
    RunTicks(kSettleTicks);
  }

  bool AwaitingResync() { return client_.World().get<StateDigests>().awaiting_resync.has_value(); }

  static Eigen::Vector3f Position(Z13TestWorld& world, uint32_t player_id) {
    const flecs::entity player = world.World().lookup(z13::gameplay::PlayerEntityName(player_id).c_str());
    EXPECT_TRUE(player) << "no player " << player_id;
    return player ? z13::math::ExtractTranslation<float>(player.get<Eigen::Matrix4f>()) : Eigen::Vector3f::Zero();
  }

  std::shared_ptr<InMemoryNetwork> network_ = std::make_shared<InMemoryNetwork>();
  Z13TestWorld server_ {
      {std::string(kServerArg), std::string(kStationSceneArg), std::string(z13::testing::kSpireScene)}, network_};
  Z13TestWorld client_ {{std::string(kConnectArg), std::string(kTestServerEndpoint)}, network_};
};

TEST_F(StationSessionTest, DISABLED_AClientJoinsAndPlaysOnSpire) {
  const auto join_start = std::chrono::steady_clock::now();
  ASSERT_TRUE(Run(kMaxJoinTicks, [&] {
    return client_.World().has<z13::gameplay::Gameplay>() &&
           client_.World().get<ConnectionStatus>().state == ConnectionState::kConnected;
  }));
  const auto joined = std::chrono::steady_clock::now();
  std::cout << "joined in " << std::chrono::duration<double>(joined - join_start).count() << " s\n";

  const Eigen::Vector3f start = Position(client_, kClientId);
  for (const Keycode key : {Keycode::KEY_W, Keycode::KEY_A, Keycode::KEY_S, Keycode::KEY_D, Keycode::KEY_SPACE}) {
    Hold(key);
  }
  const uint64_t target = client_.World().get<StateDigests>().checked + kDigestChecks;
  ASSERT_TRUE(Run(kMaxJoinTicks, [&] { return client_.World().get<StateDigests>().checked >= target; }));
  std::cout << "played in " << std::chrono::duration<double>(std::chrono::steady_clock::now() - joined).count()
            << " s\n";

  EXPECT_TRUE(client_.World().has<z13::gameplay::Gameplay>()) << "the client left the game";
  EXPECT_EQ(client_.World().get<ConnectionStatus>().state, ConnectionState::kConnected);
  EXPECT_EQ(client_.World().get<StateDigests>().resyncs, 0u);
  EXPECT_GT((Position(client_, kClientId) - start).norm(), 0.f) << "the client never moved";
  EXPECT_LT((Position(server_, kClientId) - Position(client_, kClientId)).norm(), z13::testing::kTestEpsilon)
      << "the host sees the client elsewhere";
}

// The first digest the client finds wrong, taken apart: which part differs, and both sides'
// snapshots of that tick as JSON in $Z13_DIVERGENCE_DIR (the system temp directory without it),
// for comparing component by component. Caught as the client asks for a resync, before the
// Resync replaces its history (resyncs counts only once it has).
TEST_F(StationSessionTest, DISABLED_DumpTheFirstDivergence) {
  // The client's player on every tick as each side last simulated it (a replay overwrites).
  std::map<uint64_t, std::string> server_trace;
  std::map<uint64_t, std::string> client_trace;
  for (auto [side, trace] : {std::pair {&server_, &server_trace}, std::pair {&client_, &client_trace}}) {
    side->World()
        .system<const z13::gameplay::Player, const Eigen::Matrix4f, const z13::gravity::Gravity*,
                const z13::gameplay::PlayerMotion*>("Test::TraceClientPlayer")
        .kind(flecs::PostFrame)
        .each([trace](flecs::iter& it, size_t, const z13::gameplay::Player& player, const Eigen::Matrix4f& transform,
                      const z13::gravity::Gravity* gravity, const z13::gameplay::PlayerMotion* motion) {
          if (player.id != kClientId) {
            return;
          }
          const uint64_t tick = it.world().get<z13::flecs_tools::SimulationClock>().tick;
          (*trace)[tick] += std::format("end: pos {:.4f} {:.4f} {:.4f} gravity {} grounded {} fall {:.3f}{}",
              transform(0, 3), transform(1, 3), transform(2, 3), gravity ? gravity->acceleration.z() : -1.f,
              motion ? motion->grounded : false, motion ? motion->fall_speed : 0.f,
              it.world().has<z13::flecs_tools::ReplayInProgress>() ? " (replay)" : "");
        });
    side->World()
        .system<const z13::gameplay::Player, const Eigen::Matrix4f, const z13::gravity::Gravity*>(
            "Test::TraceClientPlayerBeforeMoving")
        .kind<z13::inventory::InventoryPhase>()
        .each([trace](flecs::iter& it, size_t, const z13::gameplay::Player& player, const Eigen::Matrix4f& transform,
                      const z13::gravity::Gravity* gravity) {
          if (player.id != kClientId) {
            return;
          }
          const uint64_t tick = it.world().get<z13::flecs_tools::SimulationClock>().tick;
          (*trace)[tick] = std::format("before: pos {:.4f} {:.4f} {:.4f} gravity {} | ", transform(0, 3),
              transform(1, 3), transform(2, 3), gravity ? gravity->acceleration.z() : -1.f);
        });
  }
  ASSERT_TRUE(Run(kMaxJoinTicks, [&] {
    return client_.World().has<z13::gameplay::Gameplay>() &&
           client_.World().get<ConnectionStatus>().state == ConnectionState::kConnected;
  }));
  for (const Keycode key : {Keycode::KEY_W, Keycode::KEY_A}) {
    client_.EmitInput(KeyDown(key));
    if (Run(kActionTicks, [&] { return AwaitingResync(); })) {
      break;
    }
    client_.EmitInput(KeyUp(key));
  }
  if (!Run(kMaxJoinTicks, [&] { return AwaitingResync(); })) {
    GTEST_SKIP() << "no divergence to dump";
  }

  const auto& server_digests = server_.World().get<StateDigests>().local;
  const auto& client_digests = client_.World().get<StateDigests>().local;
  std::optional<uint64_t> tick;
  for (const auto& [at, client_digest] : client_digests) {
    const auto server_digest = server_digests.find(at);
    if (server_digest != server_digests.end() && !DigestsMatch(server_digest->second, client_digest)) {
      tick = at;
      const auto& server = server_digest->second;
      std::cout << "first diverging tick " << at << ": entities " << client_digest.entity_count << " (server "
                << server.entity_count << "), names " << (client_digest.name_hash == server.name_hash ? "same" : "differ")
                << ", player positions:\n";
      for (const auto& pose : client_digest.positions) {
        std::cout << "  client " << pose.player_id() << ": " << pose.x() << " " << pose.y() << " " << pose.z() << "\n";
      }
      for (const auto& pose : server.positions) {
        std::cout << "  server " << pose.player_id() << ": " << pose.x() << " " << pose.y() << " " << pose.z() << "\n";
      }
      break;
    }
  }
  ASSERT_TRUE(tick.has_value()) << "the resync's digests all match";

  std::optional<uint64_t> first_trace_divergence;
  for (const auto& [at, line] : client_trace) {
    const auto on_server = server_trace.find(at);
    if (at <= *tick && on_server != server_trace.end() && on_server->second != line) {
      first_trace_divergence = at;
      break;
    }
  }
  std::cout << "client player traced from tick " << (client_trace.empty() ? 0 : client_trace.begin()->first)
            << " (client), " << (server_trace.empty() ? 0 : server_trace.begin()->first) << " (server)\n";
  if (first_trace_divergence) {
    for (uint64_t at = *first_trace_divergence > 3 ? *first_trace_divergence - 3 : 0;
         at <= *first_trace_divergence + 6; ++at) {
      std::cout << "  tick " << at << "\n    server " << server_trace[at] << "\n    client " << client_trace[at] << "\n";
    }
  }
  for (auto [side, label] : {std::pair {&server_, "server"}, std::pair {&client_, "client"}}) {
    std::cout << label << " log of player " << kClientId << ":";
    for (const auto& record : side->World().get<z13::gameplay::PlayerActionLog>().log.Entries()) {
      if (record.player_id == kClientId && record.tick <= *tick) {
        std::cout << " [" << record.tick << " a" << record.action_id << "=" << record.value << "]";
      }
    }
    std::cout << "\n";
  }

  const char* dir_variable = std::getenv(z13::net::kDivergenceDirectoryVariable.data());
  const std::filesystem::path dir = dir_variable != nullptr ? std::filesystem::path(dir_variable)
                                                            : std::filesystem::temp_directory_path();
  for (auto* side : {&server_, &client_}) {
    const auto& history = side->World().get<z13::flecs_tools::WorldSnapshotHistory>().history;
    const auto entry = std::ranges::find(history.Entries(), *tick, &z13::flecs_tools::TimestampedSnapshot::tick);
    ASSERT_NE(entry, history.Entries().end()) << "no snapshot of tick " << *tick;
    const auto json = z13::flecs_tools::WorldJsonStore::ToJson(side->World(), entry->snapshot);
    ASSERT_TRUE(json.has_value()) << json.error();
    const auto path = dir / (side == &server_ ? "divergence_server.json" : "divergence_client.json");
    ASSERT_TRUE(z13::WriteFile(path, *json).has_value()) << path;
    std::cout << "wrote " << path << "\n";
  }
}

// What the host's walking costs the client's frames: each host command reaches the client after
// its tick and rolls it back.
TEST_F(StationSessionTest, DISABLED_TheHostWalkingCostsTheClient) {
  constexpr uint64_t kWalkTicks = 120;
  ASSERT_TRUE(Run(kMaxJoinTicks, [&] {
    return client_.World().has<z13::gameplay::Gameplay>() &&
           client_.World().get<ConnectionStatus>().state == ConnectionState::kConnected;
  }));
  RunTicks(kSettleTicks);
  const uint64_t rollbacks_before = client_.World().get<z13::flecs_tools::RollbackMetrics>().rollbacks;

  ecs_measure_system_time(client_.World(), true);
  const std::vector<z13::SystemTime> systems_before = z13::SystemTimes(client_.World());
  server_.EmitInput(KeyDown(Keycode::KEY_W));
  std::chrono::duration<double, std::milli> longest {};
  std::chrono::duration<double, std::milli> total {};
  for (uint64_t tick = 0; tick < kWalkTicks + kSettleTicks; ++tick) {
    if (tick == kWalkTicks) {
      server_.EmitInput(KeyUp(Keycode::KEY_W));
    }
    z13::flecs_tools::TickWorld(server_.World(), kNetTestDeltaTime);
    const auto start = std::chrono::steady_clock::now();
    z13::flecs_tools::TickWorld(client_.World(), kNetTestDeltaTime);
    const std::chrono::duration<double, std::milli> frame = std::chrono::steady_clock::now() - start;
    longest = std::max(longest, frame);
    total += frame;
    network_->Tick();
  }

  constexpr size_t kTopSystems = 10;
  const std::vector<z13::SystemTime> spent = z13::SystemTimesPerFrame(
      systems_before, z13::SystemTimes(client_.World()), static_cast<int>(kWalkTicks + kSettleTicks));
  for (const z13::SystemTime& system : spent | std::views::take(kTopSystems)) {
    std::cout << std::format("  {:7.2f} ms/frame {}\n", system.ms, system.path);
  }
  const auto& metrics = client_.World().get<z13::flecs_tools::RollbackMetrics>();
  std::cout << std::format("client frames: longest {:.1f} ms, mean {:.1f} ms; rollbacks {} (deepest {} ticks)\n",
                           longest.count(), total.count() / static_cast<double>(kWalkTicks + kSettleTicks),
                           metrics.rollbacks - rollbacks_before, metrics.max_depth_ticks);
  EXPECT_EQ(client_.World().get<StateDigests>().resyncs, 0u);
}

// What the client's longest frames while joining spend, by system: the replay from the join
// snapshot runs inside one TickWorld call.
TEST_F(StationSessionTest, DISABLED_TheJoinReplayCost) {
  constexpr size_t kTopSystems = 15;
  constexpr std::chrono::duration<double, std::milli> kSlowFrame {500.};
  ecs_measure_system_time(client_.World(), true);
  for (uint64_t tick = 0; tick < kMaxJoinTicks; ++tick) {
    z13::flecs_tools::TickWorld(server_.World(), kNetTestDeltaTime);
    const auto systems_before = z13::SystemTimes(client_.World());
    const uint64_t replayed_before = client_.World().get<z13::flecs_tools::RollbackMetrics>().replayed_ticks;
    const auto start = std::chrono::steady_clock::now();
    z13::flecs_tools::TickWorld(client_.World(), kNetTestDeltaTime);
    const std::chrono::duration<double, std::milli> frame = std::chrono::steady_clock::now() - start;
    network_->Tick();
    if (frame >= kSlowFrame) {
      const uint64_t replayed = client_.World().get<z13::flecs_tools::RollbackMetrics>().replayed_ticks - replayed_before;
      std::cout << std::format("client tick {}: frame {:.0f} ms, replayed {} ticks\n",
                               client_.World().get<z13::flecs_tools::SimulationClock>().tick, frame.count(), replayed);
      for (const z13::SystemTime& system :
           z13::SystemTimesPerFrame(systems_before, z13::SystemTimes(client_.World()), 1) | std::views::take(kTopSystems)) {
        std::cout << std::format("  {:8.1f} ms {}\n", system.ms, system.path);
      }
    }
    if (client_.World().get<ConnectionStatus>().state == ConnectionState::kConnected) {
      break;
    }
  }
  ASSERT_EQ(client_.World().get<ConnectionStatus>().state, ConnectionState::kConnected);

  // A plain rollback of the joined client, for the cost of a replayed tick itself.
  constexpr uint64_t kRollbackTicks = 90;
  RunTicks(kRollbackTicks + kSettleTicks);
  for (int attempt = 0; attempt < 2; ++attempt) {
    const uint64_t now = client_.World().get<z13::flecs_tools::SimulationClock>().tick;
    z13::flecs_tools::RequestRollback(client_.World(), now - kRollbackTicks, now);
    const auto systems_before = z13::SystemTimes(client_.World());
    const auto start = std::chrono::steady_clock::now();
    z13::flecs_tools::TickWorld(client_.World(), kNetTestDeltaTime);
    const std::chrono::duration<double, std::milli> frame = std::chrono::steady_clock::now() - start;
    std::cout << std::format("rollback {} ticks: frame {:.0f} ms\n", kRollbackTicks, frame.count());
    for (const z13::SystemTime& system :
         z13::SystemTimesPerFrame(systems_before, z13::SystemTimes(client_.World()), 1) | std::views::take(kTopSystems)) {
      std::cout << std::format("  {:8.1f} ms {}\n", system.ms, system.path);
    }
  }
}

}  // namespace
}  // namespace z13::net
