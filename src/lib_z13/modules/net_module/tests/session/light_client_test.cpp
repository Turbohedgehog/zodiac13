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
#include <vector>

#include <Eigen/Dense>

#include <lib_core/state/rollback.h>
#include <z13_module/gameplay/gameplay_entities.h>
#include <z13_settings/net_tuning.h>
#include <z13_settings/settings.h>
#include <z13_tests/shipped_station.h>

#include <net_module/state_digest.h>

#include "../../../z13_module/tests/support/load_session.h"
#include "../../src/light_client.h"
#include "../../src/net_session.h"

namespace z13::net {
namespace {

constexpr uint64_t kConnectTicks = 600;
constexpr uint64_t kClockSettleTicks = 120;
constexpr uint64_t kWalkTicks = 300;
constexpr size_t kLightClients = 3;
constexpr float kMinWalkM = 2.f;

Eigen::Vector3f PositionOf(z13::testing::LoadSession& session, uint32_t player_id) {
  const flecs::entity player = session.Server().World().lookup(z13::gameplay::PlayerEntityName(player_id).c_str());
  return player.is_valid() ? Eigen::Vector3f(player.get<Eigen::Matrix4f>().block<3, 1>(0, 3)) : Eigen::Vector3f::Zero();
}

// The server takes a light client's input as a player's: on time, so none is moved or rolled back, and a full client
// simulating the light players from their commands agrees with the server.
TEST(LightClientTest, LightPlayersWalkOnTheServerOnTimeAndAFullClientAgrees) {
  z13::testing::LoadSession session(
      1, {}, [](z13::Settings& settings) { settings.net->interest_events = true; }, z13::testing::kTestScene);
  session.AddLightClients(kLightClients);
  ASSERT_TRUE(session.Connect(kConnectTicks));
  // The full client's commands are late until its clock has caught up with the lead.
  for (uint64_t tick = 0; tick < kClockSettleTicks; ++tick) {
    session.Tick();
  }
  flecs::world server = session.Server().World();
  const uint64_t settled_rollbacks = server.get<z13::flecs_tools::RollbackMetrics>().rollbacks;
  std::vector<Eigen::Vector3f> start;
  for (size_t i = 0; i < session.LightClientCount(); ++i) {
    start.push_back(PositionOf(session, session.LightClient(i).PlayerId().value()));
  }

  for (uint64_t tick = 0; tick < kWalkTicks; ++tick) {
    session.Tick();
  }

  for (size_t i = 0; i < session.LightClientCount(); ++i) {
    const LightClient& light = session.LightClient(i);
    EXPECT_FALSE(light.Failure()) << *light.Failure();
    EXPECT_GT((PositionOf(session, light.PlayerId().value()) - start[i]).norm(), kMinWalkM) << "light client " << i;
  }
  EXPECT_EQ(server.get<z13::flecs_tools::RollbackMetrics>().rollbacks, settled_rollbacks);
  EXPECT_FALSE(server.get<NetSession>().Sent().by_message.contains(fbs::net::MessageBody::CommandsRetimed));
  EXPECT_EQ(session.Client(0).World().get<StateDigests>().resyncs, 0u);
}

}  // namespace
}  // namespace z13::net
