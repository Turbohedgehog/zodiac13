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
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <lib_core/time/simulation_clock.h>

#include <net_module/in_memory_transport.h>
#include <net_module/state_digest.h>

#include <z13/components/action_hints.h>
#include <z13/components/action_map.h>
#include <z13/components/gameplay.h>
#include <z13/components/input_events.h>
#include <z13/components/net.h>
#include <z13/components/player.h>
#include <z13_module/gameplay/camera_look.h>
#include <z13_module/gameplay/gameplay_entities.h>
#include <z13_settings/settings.h>

#include "../../../net_module/src/action_hints.h"
#include "../../../net_module/src/hint_packet.h"
#include "../../../net_module/src/player_positions.h"
#include "../support/test_network.h"
#include "../support/z13_test_world.h"

// An observer predicts a remote player's look by the hints sent ahead of the confirmed commands.
namespace z13::net {
namespace {

using fbs::net::HintFilter;
using z13::testing::kConnectArg;
using z13::testing::kMaxNetTestTicks;
using z13::testing::kNetTestDeltaTime;
using z13::testing::kServerArg;
using z13::testing::kTestServerEndpoint;
using z13::testing::RunNetworkUntil;
using z13::testing::Z13TestWorld;

constexpr int kSweepTicks = 30;
constexpr int kSweeps = 4;
constexpr int kStillTicks = 40;
constexpr int kLookDelta = 6;
// The least the hints must save an observer on a 2-4 tick network (measured ~2).
constexpr double kMinSavedLagTicks = 1.0;
constexpr int kLagSkipTicks = 40;
constexpr int kLagWindow = 40;
constexpr float kStillEpsilon = 1e-4f;
// A player spawns kSpawnSpacing (3 m) from the next, and the radius setting is at least 1 m.
constexpr double kNearbyRadiusM = 30.;
constexpr double kFarRadiusM = 1.;
constexpr float kMovedM = 5.f;

bool IsConnected(Z13TestWorld& world) {
  return world.World().has<z13::gameplay::Gameplay>() &&
      world.World().get<ConnectionStatus>().state == ConnectionState::kConnected;
}

float Pitch(Z13TestWorld& world, uint32_t player_id) {
  const flecs::entity player = world.World().lookup(z13::gameplay::PlayerEntityName(player_id).c_str());
  EXPECT_TRUE(player) << "no player " << player_id;
  return player ? player.get<z13::gameplay::LookAngles>().pitch_deg : 0.f;
}

z13::input::MouseMoveEvent LookVertically(int delta) {
  z13::input::MouseMoveEvent look;
  look.delta = {.x = 0, .y = delta};
  return look;
}

z13::input::ActionId ActionNamed(Z13TestWorld& world, std::string_view name) {
  const auto& by_name = world.World().get<z13::input::ActionMap>().action_map.get<z13::input::ActionMap::ActionNameTag>();
  const auto found = by_name.find(name);
  EXPECT_NE(found, by_name.end()) << name;
  return found == by_name.end() ? 0 : found->id;
}

// Server and two clients; A sweeps its view up and down while B watches.
class HintedSession {
 public:
  HintedSession(const HintFilter& filter, double radius_m, double drop_probability = 0.) {
    z13::Settings settings = z13::testing::TestSettings();
    settings.net->look_hint_filter = filter;
    settings.net->look_hint_radius_m = radius_m;
    network_->SetFaultConfig({.drop_probability = drop_probability, .min_delay_ticks = 2, .max_delay_ticks = 4});
    server_ = std::make_unique<Z13TestWorld>(std::vector {std::string(kServerArg)}, network_, settings);
    client_a_ = std::make_unique<Z13TestWorld>(Client(), network_, settings);
    client_b_ = std::make_unique<Z13TestWorld>(Client(), network_, settings);
    connected_ = RunNetworkUntil(*network_, Worlds(), kNetTestDeltaTime, kMaxNetTestTicks, [&] {
      return IsConnected(*client_a_) && IsConnected(*client_b_);
    });
  }

  bool Connected() const { return connected_; }
  uint32_t AId() { return *client_a_->World().get<z13::gameplay::LocalPlayer>().id; }
  Z13TestWorld& Server() { return *server_; }
  Z13TestWorld& A() { return *client_a_; }
  Z13TestWorld& B() { return *client_b_; }

  void Tick() { RunNetworkUntil(*network_, Worlds(), kNetTestDeltaTime, 1, [] { return false; }); }

  // How many ticks B's picture of A's view is behind A's during the sweep, and how often B shows a
  // pitch A never had.
  struct SweepResult {
    double lag_ticks {};
    int overshoots {};
    float own_final {};
    float seen_final {};
  };
  SweepResult Sweep() {
    SweepResult result;
    std::vector<float> own;
    std::vector<float> seen;
    float lowest = Pitch(*client_a_, AId());
    float highest = lowest;
    for (int tick = 0; tick < kSweepTicks * kSweeps + kStillTicks; ++tick) {
      if (tick < kSweepTicks * kSweeps) {
        client_a_->EmitInput(LookVertically((tick / kSweepTicks) % 2 == 0 ? kLookDelta : -kLookDelta));
      }
      Tick();
      own.push_back(Pitch(*client_a_, AId()));
      seen.push_back(Pitch(*client_b_, AId()));
      lowest = std::min(lowest, own.back());
      highest = std::max(highest, own.back());
      result.overshoots +=
          (seen.back() < lowest - z13::testing::kTestEpsilon || seen.back() > highest + z13::testing::kTestEpsilon);
    }
    // The lag at a tick is how far back in A's history B's pitch is found.
    double total = 0.;
    int counted = 0;
    for (int tick = kLagSkipTicks; tick < kSweepTicks * kSweeps - kLagSkipTicks; ++tick) {
      if (std::abs(own[tick] - own[tick - 1]) < kStillEpsilon) {
        continue;
      }
      int best_lag = 0;
      float best_error = std::abs(seen[tick] - own[tick]);
      for (int lag = 1; lag < kLagWindow; ++lag) {
        const float error = std::abs(seen[tick] - own[tick - lag]);
        if (error < best_error) {
          best_error = error;
          best_lag = lag;
        }
      }
      total += best_lag;
      ++counted;
    }
    result.lag_ticks = counted > 0 ? total / counted : 0.;
    result.own_final = own.back();
    result.seen_final = seen.back();
    return result;
  }

 private:
  static std::vector<std::string> Client() { return {std::string(kConnectArg), std::string(kTestServerEndpoint)}; }
  std::vector<std::reference_wrapper<Z13TestWorld>> Worlds() { return {*server_, *client_a_, *client_b_}; }

  std::shared_ptr<InMemoryNetwork> network_ = std::make_shared<InMemoryNetwork>();
  std::unique_ptr<Z13TestWorld> server_;
  std::unique_ptr<Z13TestWorld> client_a_;
  std::unique_ptr<Z13TestWorld> client_b_;
  bool connected_ {};
};

TEST(ActionHintsTest, AnObserverLagsARemoteLookLessWithHints) {
  HintedSession hinted(HintFilter::Distance, kNearbyRadiusM);
  HintedSession unhinted(HintFilter::None, kNearbyRadiusM);
  ASSERT_TRUE(hinted.Connected() && unhinted.Connected());

  const auto with = hinted.Sweep();
  const auto without = unhinted.Sweep();

  EXPECT_GT(without.lag_ticks, 0.);
  EXPECT_LT(with.lag_ticks, without.lag_ticks - kMinSavedLagTicks);
  EXPECT_EQ(with.overshoots, 0);
}

TEST(ActionHintsTest, LostHintsLeaveEveryoneInTheServersState) {
  constexpr double kDropProbability = 0.5;
  HintedSession session(HintFilter::Distance, kNearbyRadiusM, kDropProbability);
  ASSERT_TRUE(session.Connected());

  const auto result = session.Sweep();

  EXPECT_EQ(result.overshoots, 0);
  EXPECT_NEAR(result.seen_final, result.own_final, z13::testing::kTestEpsilon);
  EXPECT_NEAR(Pitch(session.Server(), session.AId()), Pitch(session.A(), session.AId()), z13::testing::kTestEpsilon);
  EXPECT_EQ(session.B().World().get<StateDigests>().resyncs, 0u);
}

TEST(ActionHintsTest, OnlyTheClientsInTheFilterGetHints) {
  const auto hints_heard = [](const HintFilter& filter, double radius_m) {
    HintedSession session(filter, radius_m);
    EXPECT_TRUE(session.Connected());
    session.Sweep();
    return session.B().World().get<z13::gameplay::ActionHints>().by_action.size();
  };

  EXPECT_GT(hints_heard(HintFilter::Distance, kNearbyRadiusM), 0u);
  EXPECT_GT(hints_heard(HintFilter::All, kFarRadiusM), 0u);
  EXPECT_EQ(hints_heard(HintFilter::Distance, kFarRadiusM), 0u);
  EXPECT_EQ(hints_heard(HintFilter::None, kNearbyRadiusM), 0u);
}

TEST(ActionHintsTest, TheServerRelaysOnlyHintActionsAndNoRepeat) {
  HintedSession session(HintFilter::All, kNearbyRadiusM);
  ASSERT_TRUE(session.Connected());
  flecs::world server = session.Server().World();
  auto& net = server.get_mut<NetSession>();
  const auto connections = net.PlayerConnections();
  const auto a_connection = std::ranges::find(connections, session.AId(), &std::pair<ConnectionId, uint32_t>::second);
  ASSERT_NE(a_connection, connections.end());
  const uint64_t now = server.get<z13::flecs_tools::SimulationClock>().tick;
  const z13::input::ActionId look = ActionNamed(session.Server(), "VERTICAL_LOOK");
  const z13::input::ActionId forward = ActionNamed(session.Server(), "MOVE_FORWARD");

  HintPacket packet {.tick = now + 1,
                     .hints = {{.action_id = static_cast<uint16_t>(forward), .value = 100},
                               {.action_id = static_cast<uint16_t>(look), .value = 4500}}};
  HandleHintPacket(net, server, a_connection->first, EncodeOwnHints(packet));
  for (int tick = 0; tick < kStillTicks; ++tick) {
    session.Tick();
  }
  const auto& heard = session.B().World().get<z13::gameplay::ActionHints>().by_action;

  EXPECT_FALSE(heard.contains({session.AId(), forward})) << "an action without a hint class was relayed";
  ASSERT_TRUE(heard.contains({session.AId(), look}));
  EXPECT_FLOAT_EQ(heard.at({session.AId(), look}).back().value, 45.f);

  // The same tick again is a repeat or a stale packet.
  packet.hints = {{.action_id = static_cast<uint16_t>(look), .value = 9000}};
  HandleHintPacket(net, server, a_connection->first, EncodeOwnHints(packet));
  for (int tick = 0; tick < kStillTicks; ++tick) {
    session.Tick();
  }
  EXPECT_FLOAT_EQ(session.B().World().get<z13::gameplay::ActionHints>().by_action.at({session.AId(), look}).back().value, 45.f);
}

TEST(ActionHintsTest, PlayerPositionsAreReadOncePerTick) {
  HintedSession session(HintFilter::Distance, kNearbyRadiusM);
  ASSERT_TRUE(session.Connected());
  flecs::world server = session.Server().World();
  const flecs::entity player = server.lookup(z13::gameplay::PlayerEntityName(session.AId()).c_str());
  ASSERT_TRUE(player);
  const auto position_of_a = [&] { return CurrentPlayerPositions(server).by_player.at(session.AId()); };
  const Eigen::Vector3f before = position_of_a();

  player.get_mut<Eigen::Matrix4f>()(0, 3) += kMovedM;
  EXPECT_EQ(position_of_a(), before) << "the world was read again within the tick";

  session.Tick();
  EXPECT_EQ(position_of_a(), Eigen::Vector3f(player.get<Eigen::Matrix4f>().block<3, 1>(0, 3)));
}

TEST(ActionHintsTest, AHintStandsInOnlyUntilTheConfirmedCommandsReachItsTick) {
  z13::gameplay::ActionHints hints;
  constexpr uint32_t kPlayer = 1;
  constexpr z13::input::ActionId kAction = 7;
  hints.Add(kPlayer, kAction, {.tick = 10, .value = 1.f});
  hints.Add(kPlayer, kAction, {.tick = 20, .value = 2.f});
  hints.Add(kPlayer, kAction, {.tick = 15, .value = 9.f});  // out of order: stale

  EXPECT_EQ(hints.ValueAt(kPlayer, kAction, 9, 0), std::nullopt);
  EXPECT_EQ(hints.ValueAt(kPlayer, kAction, 12, 0), 1.f);
  EXPECT_EQ(hints.ValueAt(kPlayer, kAction, 30, 0), 2.f);
  EXPECT_EQ(hints.ValueAt(kPlayer, kAction, 30, 20), std::nullopt) << "confirmed through the hint's tick";
  EXPECT_EQ(hints.ValueAt(kPlayer, kAction, 30, 15), 2.f);
  EXPECT_EQ(hints.ValueAt(kPlayer + 1, kAction, 30, 0), std::nullopt);

  for (uint64_t tick = 30; tick < 30 + 2 * z13::gameplay::ActionHints::kKeptPerAction; ++tick) {
    hints.Add(kPlayer, kAction, {.tick = tick, .value = 3.f});
  }
  EXPECT_EQ(hints.by_action.at({kPlayer, kAction}).size(), z13::gameplay::ActionHints::kKeptPerAction);
}

}  // namespace
}  // namespace z13::net
