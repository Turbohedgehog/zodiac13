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

#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <Eigen/Dense>

#include <lib_core/state/rollback.h>
#include <lib_core/utils/math.h>

#include <net_module/in_memory_transport.h>
#include <net_module/state_digest.h>

#include <z13/components/gameplay.h>
#include <z13/components/input.h>
#include <z13/components/net.h>
#include <z13/components/player_action.h>
#include <z13_module/gameplay/camera_look.h>
#include <z13_module/gameplay/gameplay_entities.h>
#include <z13_settings/settings.h>

#include "../support/building_test_helpers.h"
#include "../support/motion_metrics.h"
#include "../support/test_network.h"
#include "../support/z13_test_world.h"

namespace z13::net {
namespace {

using z13::fbs::input::Keycode;
using z13::fbs::net::RemoteInputPrediction;
using z13::testing::kConnectArg;
using z13::testing::kMaxNetTestTicks;
using z13::testing::kNetTestDeltaTime;
using z13::testing::kServerArg;
using z13::testing::kTestEpsilon;
using z13::testing::kTestServerEndpoint;
using z13::testing::KeyDown;
using z13::testing::KeyUp;
using z13::testing::RunNetworkUntil;
using z13::testing::Z13TestWorld;

constexpr int kMaxLagTicks = 60;
const float kStep = z13::gameplay::kCameraVelocity * kNetTestDeltaTime;

// Server and two clients over a 2-4 tick network, all on one prediction mode.
class RemotePredictionSession {
 public:
  explicit RemotePredictionSession(RemoteInputPrediction prediction)
      : settings_(SettingsFor(prediction)),
        server_(/*skip_main_menu=*/false, {std::string(kServerArg)}, network_, settings_),
        client_a_(/*skip_main_menu=*/false, ClientArgs(), network_, settings_),
        client_b_(/*skip_main_menu=*/false, ClientArgs(), network_, settings_) {}

  // Until B has spawned A too.
  bool Connect() {
    return RunNetworkUntil(*network_, {server_, client_a_, client_b_}, kNetTestDeltaTime, kMaxNetTestTicks, [&] {
      return IsConnected(client_a_) && IsConnected(client_b_) &&
          client_b_.World().lookup(z13::gameplay::PlayerEntityName(AId()).c_str());
    });
  }

  void Tick() { RunNetworkUntil(*network_, {server_, client_a_, client_b_}, kNetTestDeltaTime, 1, [] { return false; }); }

  uint32_t AId() { return *client_a_.World().get<z13::gameplay::LocalPlayer>().id; }

  Eigen::Vector3f PositionOfA(Z13TestWorld& world) {
    const flecs::entity player = world.World().lookup(z13::gameplay::PlayerEntityName(AId()).c_str());
    EXPECT_TRUE(player) << "no player " << AId();
    return player ? z13::math::ExtractTranslation<float>(player.get<Eigen::Matrix4f>()) : Eigen::Vector3f::Zero();
  }

  Z13TestWorld& Server() { return server_; }
  Z13TestWorld& A() { return client_a_; }
  Z13TestWorld& B() { return client_b_; }

 private:
  static z13::Settings SettingsFor(RemoteInputPrediction prediction) {
    z13::Settings settings = z13::MakeSettings();
    settings.net->remote_input_prediction = prediction;
    return settings;
  }

  static std::vector<std::string> ClientArgs() {
    return {std::string(kConnectArg), std::string(kTestServerEndpoint)};
  }

  static bool IsConnected(Z13TestWorld& world) {
    return world.World().has<z13::gameplay::Gameplay>() &&
        world.World().get<ConnectionStatus>().state == ConnectionState::kConnected;
  }

  static std::shared_ptr<InMemoryNetwork> DelayedNetwork() {
    auto network = std::make_shared<InMemoryNetwork>();
    network->SetFaultConfig({.min_delay_ticks = 2, .max_delay_ticks = 4});
    return network;
  }

  std::shared_ptr<InMemoryNetwork> network_ = DelayedNetwork();
  z13::Settings settings_;
  Z13TestWorld server_;
  Z13TestWorld client_a_;
  Z13TestWorld client_b_;
};

struct RunStopResult {
  z13::testing::TrackingMetrics metrics;
  Eigen::Vector3f a_final = Eigen::Vector3f::Zero();
  Eigen::Vector3f b_final = Eigen::Vector3f::Zero();
  Eigen::Vector3f server_final = Eigen::Vector3f::Zero();
};

// A holds W for `hold_ticks`, then stands still; B's view of A is tracked against A's own.
RunStopResult RunAndStop(RemoteInputPrediction prediction, int hold_ticks) {
  constexpr int kStillTicks = 60;
  RemotePredictionSession session(prediction);
  EXPECT_TRUE(session.Connect());
  std::vector<Eigen::Vector3f> truth {session.PositionOfA(session.A())};
  std::vector<Eigen::Vector3f> observed {session.PositionOfA(session.B())};
  session.A().EmitInput(KeyDown(Keycode::KEY_W));
  for (int tick = 0; tick < hold_ticks + kStillTicks; ++tick) {
    if (tick == hold_ticks) {
      session.A().EmitInput(KeyUp(Keycode::KEY_W));
    }
    session.Tick();
    truth.push_back(session.PositionOfA(session.A()));
    observed.push_back(session.PositionOfA(session.B()));
  }
  return {
      .metrics = z13::testing::MeasureTracking(truth, observed, kMaxLagTicks),
      .a_final = truth.back(),
      .b_final = observed.back(),
      .server_final = session.PositionOfA(session.Server()),
  };
}

TEST(RemotePredictionTest, LookActionsAreAbsoluteAndMovesAreNot) {
  Z13TestWorld world;
  const auto& by_name = world.World().get<z13::input::ActionMap>().action_map.get<z13::input::ActionMap::ActionNameTag>();
  const auto absolute = [&by_name](std::string_view name) {
    const auto found = by_name.find(name);
    EXPECT_NE(found, by_name.end()) << name;
    return found != by_name.end() && found->absolute;
  };

  EXPECT_TRUE(absolute("HORIZONTAL_LOOK"));
  EXPECT_TRUE(absolute("VERTICAL_LOOK"));
  EXPECT_FALSE(absolute("MOVE_FORWARD"));
}

// Holding the last value runs a remote player on past its stop; releasing never does.
TEST(RemotePredictionTest, NeutralPredictionNeverRunsARemotePlayerPastItsStop) {
  constexpr int kHoldTicks = 60;
  const RunStopResult hold = RunAndStop(RemoteInputPrediction::Hold, kHoldTicks);
  const RunStopResult neutral = RunAndStop(RemoteInputPrediction::Neutral, kHoldTicks);

  EXPECT_GT(hold.metrics.max_off_path, kStep) << "the scenario never overshoots, so it proves nothing";
  EXPECT_LE(neutral.metrics.max_off_path, kTestEpsilon);
  EXPECT_LT((neutral.b_final - neutral.a_final).norm(), kTestEpsilon);
  EXPECT_LT((neutral.server_final - neutral.a_final).norm(), kTestEpsilon);
}

// Past the held-key reassert interval, only the confirmed-tick heartbeat keeps B's A moving.
TEST(RemotePredictionTest, NeutralPredictionKeepsALongHoldMoving) {
  constexpr int kLongHoldTicks = 150;
  constexpr int kMaxExpectedLagTicks = 20;
  const RunStopResult neutral = RunAndStop(RemoteInputPrediction::Neutral, kLongHoldTicks);

  EXPECT_LE(neutral.metrics.lag_ticks, kMaxExpectedLagTicks);
  ASSERT_TRUE(neutral.metrics.path_ratio);
  EXPECT_NEAR(*neutral.metrics.path_ratio, 1.f, kTestEpsilon);
  EXPECT_LT((neutral.b_final - neutral.a_final).norm(), kTestEpsilon);
}

// Without confirmations through the wait, the server would stand A still for all of it:
// too deep to replay, and the press itself may have aged out of the log.
TEST(RemotePredictionTest, AWaitForAResyncKeepsConfirmingAHeldAction) {
  RemotePredictionSession session(RemoteInputPrediction::Neutral);
  ASSERT_TRUE(session.Connect());
  session.A().EmitInput(KeyDown(Keycode::KEY_W));
  for (int tick = 0; tick < kMaxLagTicks; ++tick) {
    session.Tick();
  }

  const uint64_t retention_ticks = static_cast<uint64_t>(std::llround(
      session.Server().Config().GetSnapshotRetentionSeconds() * session.Server().Config().GetFPS()));
  session.A().World().get_mut<StateDigests>().awaiting_resync = true;
  for (uint64_t tick = 0; tick <= retention_ticks; ++tick) {
    session.Tick();
  }
  session.A().World().get_mut<StateDigests>().awaiting_resync = false;
  session.A().EmitInput(KeyUp(Keycode::KEY_W));
  for (int tick = 0; tick < kMaxLagTicks; ++tick) {
    session.Tick();
  }

  for (Z13TestWorld* world : {&session.Server(), &session.B()}) {
    EXPECT_FALSE(world->World().has<z13::flecs_tools::RollbackFailed>());
    EXPECT_LT(world->World().get<z13::flecs_tools::RollbackMetrics>().max_depth_ticks, retention_ticks);
  }
  EXPECT_EQ(session.A().World().get<StateDigests>().resyncs, 0u) << "A diverged during the wait";
  EXPECT_EQ(session.B().World().get<StateDigests>().resyncs, 0u);
  EXPECT_LT((session.PositionOfA(session.B()) - session.PositionOfA(session.A())).norm(), kTestEpsilon);
  EXPECT_LT((session.PositionOfA(session.Server()) - session.PositionOfA(session.A())).norm(), kTestEpsilon);
}

// Kept while a rollback may still cross the leave, dropped once none can.
TEST(RemotePredictionTest, ADepartedPlayersConfirmedTickOutlivesTheRollbackWindowOnly) {
  RemotePredictionSession session(RemoteInputPrediction::Neutral);
  ASSERT_TRUE(session.Connect());
  const uint32_t a_id = session.AId();
  const auto confirmed = [&session, a_id] {
    return session.Server().World().get<z13::gameplay::ConfirmedInputTicks>().by_player.contains(a_id);
  };
  session.A().EmitInput(KeyDown(Keycode::KEY_W));
  for (int tick = 0; tick < kMaxLagTicks && !confirmed(); ++tick) {
    session.Tick();
  }
  ASSERT_TRUE(confirmed()) << "A's input never got a confirmed tick on the server";

  session.A().World().remove<z13::gameplay::Gameplay>();
  int ticks = 0;
  while (session.Server().World().lookup(z13::gameplay::PlayerEntityName(a_id).c_str()) && ticks < kMaxNetTestTicks) {
    session.Tick();
    ++ticks;
  }
  ASSERT_FALSE(session.Server().World().lookup(z13::gameplay::PlayerEntityName(a_id).c_str())) << "A never left";
  EXPECT_TRUE(confirmed()) << "dropped while a rollback could still cross the leave";

  const uint64_t retention_ticks = static_cast<uint64_t>(std::llround(
      session.Server().Config().GetSnapshotRetentionSeconds() * session.Server().Config().GetFPS()));
  for (uint64_t tick = 0; tick <= retention_ticks + 1; ++tick) {
    session.Tick();
  }
  EXPECT_FALSE(confirmed());
}

}  // namespace
}  // namespace z13::net
