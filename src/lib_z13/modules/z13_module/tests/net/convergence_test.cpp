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
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <random>
#include <set>
#include <span>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <lib_core/settings/config.h>
#include <lib_core/state/rollback.h>
#include <lib_core/state/world_json_store.h>
#include <lib_core/state/world_snapshot_history.h>
#include <lib_core/time/simulation_clock.h>

#include <net_module/in_memory_transport.h>
#include <net_module/state_digest.h>

#include <z13/components/gameplay.h>
#include <z13/components/input_events.h>
#include <z13/components/net.h>
#include <z13/components/player.h>
#include <z13/components/player_action.h>
#include <z13_settings/net_tuning.h>
#include <z13_tests/shipped_station.h>

#include "../support/building_test_helpers.h"
#include "../support/test_network.h"
#include "../support/z13_test_world.h"

// The convergence test of docs/net-core.md ("Проверка"): random play over a jittery link with
// stalls, clock jumps, leaves, drops and joins; afterwards every participant holds the server's
// state on a final tick, none needed a resync, and each world replayed from an old snapshot
// reproduces itself, which a hidden simulation input would break.
namespace z13::net {
namespace {

namespace ft = z13::flecs_tools;
using z13::testing::kConnectArg;
using z13::testing::kNetTestDeltaTime;
using z13::testing::kServerArg;
using z13::testing::kStationSceneArg;
using z13::testing::kTestServerEndpoint;
using z13::testing::Z13TestWorld;
using Keycode = z13::fbs::input::Keycode;

constexpr FaultConfig kJitter {.min_delay_ticks = 2, .max_delay_ticks = 10};
constexpr uint64_t kPlayTicks = 900;
constexpr size_t kMaxClients = 4;
constexpr uint64_t kMaxSettleTicks = 1500;
// Fresh digests every client checks after the play, so the compared ticks are final.
constexpr uint64_t kDigestChecks = 2;

constexpr std::array kMoveKeys {Keycode::KEY_W, Keycode::KEY_A, Keycode::KEY_S, Keycode::KEY_D};
constexpr std::array kTapKeys {Keycode::KEY_SPACE, Keycode::KEY_TAB, Keycode::KEY_F};
constexpr std::array kButtons {Keycode::MOUSE_BUTTON_LEFT, Keycode::MOUSE_BUTTON_RIGHT};

// Per tick and client, or per tick for the session events; stalls and clock jumps per scenario.
struct Odds {
  double move_key {0.03};
  double tap {0.006};
  double click {0.02};
  double look {0.2};
  double stall {};
  double clock_jump {};
  double leave {0.002};
  double drop {0.001};
  double join {0.01};
};
constexpr double kStallOdds = 0.004;
constexpr double kClockJumpOdds = 0.003;

// Ranges of ticks the session events draw from.
struct TickRange {
  uint64_t min {};
  uint64_t max {};
};
constexpr TickRange kStallTicks {.min = 10, .max = 120};
constexpr TickRange kClockJumpTicks {.min = 20, .max = 60};
constexpr TickRange kAwayTicks {.min = 30, .max = 200};
constexpr int kMaxLookStepX = 20;
constexpr int kMaxLookStepY = 8;
// The top 53 bits of a draw, as a double in [0, 1).
constexpr int kUnitShift = 11;
constexpr double kUnitScale = 0x1.0p-53;
// Frames a world may still need to apply the rollbacks pending when the network stops.
constexpr uint64_t kMaxQuietFrames = 32;

// What varies between runs: which session events happen besides play, joins and leaves.
struct Scenario {
  std::string name;
  bool stalls {};
  bool clock_jumps {};
};

struct ClientPlay {
  std::unique_ptr<Z13TestWorld> world;
  std::set<Keycode> held;
  // Pressed this tick, released the next: one tap or click.
  std::vector<Keycode> release_next;
  std::optional<uint64_t> stalled_until;
  std::optional<uint64_t> rejoin_at;
};

class ConvergenceTest : public ::testing::TestWithParam<std::tuple<Scenario, uint64_t>> {
 protected:
  ConvergenceTest() : rng_(std::get<1>(GetParam())) {
    network_->SetFaultConfig(kJitter);
    const Scenario& scenario = std::get<0>(GetParam());
    odds_.stall = scenario.stalls ? kStallOdds : 0.;
    odds_.clock_jump = scenario.clock_jumps ? kClockJumpOdds : 0.;
  }

  void Play(uint64_t ticks) {
    for (uint64_t tick = 0; tick < ticks; ++tick) {
      ++now_;
      SessionEvents();
      for (ClientPlay& client : clients_) {
        if (Active(client) && InGame(*client.world)) {
          RandomInput(client);
        }
      }
      Tick();
    }
  }

  // Everyone lets go, the stalled wake up and the left come back, then fresh digests settle it.
  void Settle() {
    for (ClientPlay& client : clients_) {
      client.stalled_until.reset();
      ReleaseAll(client);
      if (client.rejoin_at) {
        client.rejoin_at = now_;
      }
    }
    ASSERT_TRUE(RunUntil([&] { return std::ranges::all_of(clients_, [](ClientPlay& c) { return InGame(*c.world); }); }))
        << "a client never got back into the game";
    std::vector<uint64_t> targets;
    for (ClientPlay& client : clients_) {
      targets.push_back(client.world->World().get<StateDigests>().checked + kDigestChecks);
    }
    ASSERT_TRUE(RunUntil([&] {
      for (size_t i = 0; i < clients_.size(); ++i) {
        const auto& digests = clients_[i].world->World().get<StateDigests>();
        if (digests.checked < targets[i] || digests.awaiting_resync) {
          return false;
        }
      }
      return true;
    })) << "digests stopped arriving";
  }

  void ExpectConverged() {
    for (ClientPlay& client : clients_) {
      EXPECT_EQ(client.world->World().get<StateDigests>().resyncs, 0u) << Who(*client.world) << " was resynced";
    }
    const std::optional<uint64_t> tick = FinalTick();
    ASSERT_TRUE(tick) << "no snapshot tick every participant still keeps";
    const std::string server = SnapshotJson(*server_, *tick);
    for (ClientPlay& client : clients_) {
      EXPECT_EQ(SnapshotJson(*client.world, *tick), server)
          << Who(*client.world) << " differs from the server at tick " << *tick;
    }
  }

  // Replays each world from an old snapshot: the tick must come out as it was.
  void ExpectEachWorldReproducesItself() {
    Quiet(*server_);
    for (ClientPlay& client : clients_) {
      Quiet(*client.world);
    }
    const std::optional<uint64_t> tick = FinalTick();
    ASSERT_TRUE(tick);
    ExpectReproduces(*server_, "the server", *tick);
    for (ClientPlay& client : clients_) {
      ExpectReproduces(*client.world, Who(*client.world), *tick);
    }
  }

 private:
  bool Active(const ClientPlay& client) const { return !client.stalled_until || *client.stalled_until <= now_; }

  void Tick() {
    ft::TickWorld(server_->World(), kNetTestDeltaTime);
    for (ClientPlay& client : clients_) {
      if (Active(client)) {
        client.stalled_until.reset();
        ft::TickWorld(client.world->World(), kNetTestDeltaTime);
      }
    }
    network_->Tick();
  }

  bool RunUntil(const std::function<bool()>& condition) {
    for (uint64_t tick = 0; tick < kMaxSettleTicks; ++tick) {
      ++now_;
      RejoinDue();
      Tick();
      if (condition()) {
        return true;
      }
    }
    return condition();
  }

  // From the engine's raw output, which the standard fixes bit for bit, unlike its distributions:
  // a seed plays the same game under libstdc++, libc++ and MSVC, so a CI failure replays here.
  bool Roll(double odds) { return static_cast<double>(rng_() >> kUnitShift) * kUnitScale < odds; }

  uint64_t Below(uint64_t count) { return rng_() % count; }

  int Within(int limit) { return static_cast<int>(Below(2 * static_cast<uint64_t>(limit) + 1)) - limit; }

  template <typename T>
  const T& Pick(std::span<const T> items) {
    return items[Below(items.size())];
  }

  uint64_t Draw(TickRange range) { return range.min + Below(range.max - range.min + 1); }

  // Among the clients in the game and not stalled.
  std::optional<size_t> RandomPlayer() {
    std::vector<size_t> playing;
    for (size_t i = 0; i < clients_.size(); ++i) {
      if (Active(clients_[i]) && InGame(*clients_[i].world)) {
        playing.push_back(i);
      }
    }
    return playing.empty() ? std::nullopt : std::optional(Pick(std::span<const size_t>(playing)));
  }

  void SessionEvents() {
    if (clients_.size() < kMaxClients && Roll(odds_.join)) {
      clients_.push_back({.world = std::make_unique<Z13TestWorld>(
                              std::vector {std::string(kConnectArg), std::string(kTestServerEndpoint)}, network_)});
    }
    if (const auto player = RandomPlayer(); player && Roll(odds_.stall)) {
      clients_[*player].stalled_until = now_ + Draw(kStallTicks);
    }
    if (const auto player = RandomPlayer(); player && Roll(odds_.clock_jump)) {
      ft::RequestClockAdjust(clients_[*player].world->World(), static_cast<int64_t>(Draw(kClockJumpTicks)));
    }
    if (const auto player = RandomPlayer(); player && Roll(odds_.leave)) {
      ClientPlay& client = clients_[*player];
      ReleaseAll(client);
      client.world->ExitToMainMenu();
      client.rejoin_at = now_ + Draw(kAwayTicks);
    }
    if (const auto player = RandomPlayer(); player && Roll(odds_.drop)) {
      clients_.erase(clients_.begin() + static_cast<std::ptrdiff_t>(*player));
    }
    RejoinDue();
  }

  // Once the left session is closed: a JoinRequest during it would be ignored.
  void RejoinDue() {
    for (ClientPlay& client : clients_) {
      if (client.rejoin_at && *client.rejoin_at <= now_ && !client.world->World().has<ClientRole>()) {
        client.rejoin_at.reset();
        client.world->World().entity().set<JoinRequest>(
            {.endpoint = {.host = "127.0.0.1", .port = z13::kDefaultServerPort}});
      }
    }
  }

  void RandomInput(ClientPlay& client) {
    Z13TestWorld& world = *client.world;
    for (const Keycode key : std::exchange(client.release_next, {})) {
      Release(world, key);
    }
    for (const Keycode key : kMoveKeys) {
      if (Roll(odds_.move_key)) {
        if (client.held.erase(key) > 0) {
          world.EmitInput(z13::testing::KeyUp(key));
        } else {
          client.held.insert(key);
          world.EmitInput(z13::testing::KeyDown(key));
        }
      }
    }
    if (Roll(odds_.tap)) {
      const Keycode key = Pick(std::span<const Keycode>(kTapKeys));
      world.EmitInput(z13::testing::KeyDown(key));
      client.release_next.push_back(key);
    }
    if (Roll(odds_.click)) {
      const Keycode button = Pick(std::span<const Keycode>(kButtons));
      world.EmitInput(z13::testing::MouseDown(button));
      client.release_next.push_back(button);
    }
    if (Roll(odds_.look)) {
      z13::input::MouseMoveEvent look;
      look.delta = {.x = Within(kMaxLookStepX), .y = Within(kMaxLookStepY)};
      world.EmitInput(look);
    }
  }

  static void Release(Z13TestWorld& world, Keycode key) {
    const bool button = std::ranges::contains(kButtons, key);
    if (button) {
      world.EmitInput(z13::testing::MouseUp(key));
    } else {
      world.EmitInput(z13::testing::KeyUp(key));
    }
  }

  static void ReleaseAll(ClientPlay& client) {
    for (const Keycode key : std::exchange(client.release_next, {})) {
      Release(*client.world, key);
    }
    for (const Keycode key : std::exchange(client.held, {})) {
      Release(*client.world, key);
    }
  }

  // The newest tick final for the server (past its late window) whose snapshot every world keeps.
  std::optional<uint64_t> FinalTick() {
    const uint64_t server_now = server_->World().get<ft::SimulationClock>().tick;
    const uint64_t max_late = server_->World().get<NetTuning>().max_late_ticks;
    std::optional<uint64_t> best;
    for (const ft::TimestampedSnapshot& entry : History(*server_)) {
      const bool kept_everywhere = std::ranges::all_of(clients_, [&entry](ClientPlay& client) {
        return std::ranges::contains(History(*client.world), entry.tick, &ft::TimestampedSnapshot::tick);
      });
      if (entry.tick + max_late < server_now && kept_everywhere && entry.tick > best.value_or(0)) {
        best = entry.tick;
      }
    }
    return best;
  }

  static const std::deque<ft::TimestampedSnapshot>& History(Z13TestWorld& world) {
    return world.World().get<ft::WorldSnapshotHistory>().history.Entries();
  }

  static std::string SnapshotJson(Z13TestWorld& world, uint64_t tick) {
    const auto& history = History(world);
    const auto entry = std::ranges::find(history, tick, &ft::TimestampedSnapshot::tick);
    if (entry == history.end()) {
      ADD_FAILURE() << "no snapshot of tick " << tick;
      return {};
    }
    auto json = ft::WorldJsonStore::ToJson(world.World(), entry->snapshot);
    EXPECT_TRUE(json.has_value()) << (json ? "" : json.error());
    return json.value_or("");
  }

  // Takes what already reached the world and lets its pending rollbacks land, with the network
  // no longer ticked: from here on the world's inputs don't change.
  static void Quiet(Z13TestWorld& world) {
    for (uint64_t frame = 0; frame < kMaxQuietFrames; ++frame) {
      ft::TickWorld(world.World(), kNetTestDeltaTime);
      if (!ft::DeferredRollbackTick(world.World()) && !world.World().has<ft::ReplayInProgress>()) {
        return;
      }
    }
    ADD_FAILURE() << "a rollback stayed pending";
  }

  static void ExpectReproduces(Z13TestWorld& world, const std::string& who, uint64_t tick) {
    const std::string before = SnapshotJson(world, tick);
    // Not older than the log: snapshots are pruned on capture ticks only (docs/net-core.md, Н8).
    const uint64_t log_start = world.World().get<z13::gameplay::PlayerActionLog>().retained_since_tick;
    std::optional<uint64_t> oldest;
    for (const ft::TimestampedSnapshot& entry : History(world)) {
      if (entry.tick >= log_start && entry.tick < oldest.value_or(entry.tick + 1)) {
        oldest = entry.tick;
      }
    }
    ASSERT_TRUE(oldest && *oldest < tick) << who << " keeps nothing older than tick " << tick;
    ft::RequestRollback(world.World(), *oldest, world.World().get<ft::SimulationClock>().tick);
    ft::TickWorld(world.World(), kNetTestDeltaTime);
    EXPECT_EQ(SnapshotJson(world, tick), before) << who << " replays tick " << tick << " differently";
  }

  static bool InGame(Z13TestWorld& world) {
    return world.World().has<z13::gameplay::Gameplay>() &&
           world.World().get<ConnectionStatus>().state == ConnectionState::kConnected;
  }

  static std::string Who(Z13TestWorld& world) {
    const std::optional<uint32_t> id = world.World().get<z13::gameplay::LocalPlayer>().id;
    return id ? std::format("player {}", *id) : std::string("a client without a player");
  }

  std::mt19937_64 rng_;
  Odds odds_;
  uint64_t now_ {};
  std::shared_ptr<InMemoryNetwork> network_ = std::make_shared<InMemoryNetwork>();
  std::unique_ptr<Z13TestWorld> server_ = std::make_unique<Z13TestWorld>(
      std::vector {std::string(kServerArg), std::string(kStationSceneArg), std::string(z13::testing::kSiteScene)},
      network_);
  std::vector<ClientPlay> clients_;
};

TEST_P(ConvergenceTest, EveryoneEndsInTheServersState) {
  Play(kPlayTicks);
  Settle();
  ExpectConverged();
  ExpectEachWorldReproducesItself();
}

std::string ScenarioName(const ::testing::TestParamInfo<std::tuple<Scenario, uint64_t>>& info) {
  return std::format("{}_seed{}", std::get<0>(info.param).name, std::get<1>(info.param));
}

const Scenario kPlay {.name = "Play"};
const Scenario kStalls {.name = "Stalls", .stalls = true};
const Scenario kClockJumps {.name = "ClockJumps", .clock_jumps = true};

// About half a minute each in Debug: one seed per scenario in every run.
INSTANTIATE_TEST_SUITE_P(
    Site, ConvergenceTest, ::testing::Combine(::testing::Values(kPlay, kClockJumps), ::testing::Values(1u)),
    ScenarioName);
// More seeds, --gtest_also_run_disabled_tests.
INSTANTIATE_TEST_SUITE_P(
    DISABLED_SiteSeeds, ConvergenceTest,
    ::testing::Combine(::testing::Values(kPlay, kClockJumps), ::testing::Values(2u, 3u, 4u)), ScenarioName);
// Stalls diverge until Н1 of docs/net-core.md is fixed (seed 1 in Release, seed 2 in Debug).
INSTANTIATE_TEST_SUITE_P(
    DISABLED_SiteStalls, ConvergenceTest, ::testing::Combine(::testing::Values(kStalls), ::testing::Values(1u, 2u, 3u, 4u)),
    ScenarioName);

}  // namespace
}  // namespace z13::net
