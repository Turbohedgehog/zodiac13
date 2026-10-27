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
#include <memory>
#include <optional>

#include <Eigen/Dense>

#include <lib_core/state/state_markers.h>
#include <net_module/state_digest.h>
#include <z13/components/ghost.h>
#include <z13/components/player.h>
#include <z13_module/gameplay/gameplay_entities.h>
#include <z13_settings/settings.h>

#include "../../../net_module/src/ghost_players.h"
#include "../../../net_module/src/net_session.h"
#include "../support/load_session.h"

// A client is drawn the players it doesn't simulate as ghosts (docs/network-scaling-plan.md, stage 5b).
namespace z13::net {
namespace {

using z13::testing::LoadSession;

constexpr size_t kClients = 3;
constexpr uint64_t kConnectTicks = 600;
// Long enough for the walkers to leave each other's presence (the interest bench spreads in 900).
constexpr uint64_t kSpreadTicks = 900;
constexpr double kWholeStationM = 5000.;
// A ghost shows the server's pose from a pose interval, the network and the clock lead ago.
constexpr double kGhostLagM = 8.;

class GhostPosesTest : public ::testing::Test {
 protected:
  void Run(bool ghosts) {
    session_ = std::make_unique<LoadSession>(
        kClients, z13::testing::Steering {.walk = true, .look = false, .fan_out = true},
        [ghosts](z13::Settings& settings) {
          settings.net->interest_events = true;
          settings.net->ghosts = ghosts;
          settings.net->ghost_radius_m = kWholeStationM;
          settings.net->ghost_far_every = 1;
        });
    ASSERT_TRUE(session_->Connect(kConnectTicks));
    for (uint64_t tick = 0; tick < kSpreadTicks; ++tick) {
      session_->Tick();
    }
  }

  flecs::world Client(size_t index) { return session_->Client(index).World(); }

  std::optional<Eigen::Vector3f> ServerPosition(uint32_t player_id) {
    const flecs::entity player = session_->Server().World().lookup(z13::gameplay::PlayerEntityName(player_id).c_str());
    return player ? std::optional<Eigen::Vector3f>(player.get<Eigen::Matrix4f>().block<3, 1>(0, 3)) : std::nullopt;
  }

  std::unique_ptr<LoadSession> session_;
};

TEST_F(GhostPosesTest, AClientDrawsThePlayersOutsideItsPresenceAsGhosts) {
  Run(true);

  size_t ghosts = 0;
  for (size_t i = 0; i < kClients; ++i) {
    flecs::world client = Client(i);
    const auto& stored = client.get<GhostPlayers>().by_player;
    client.query_builder<const z13::gameplay::GhostPlayer, const Eigen::Matrix4f>().build().each(
        [&](flecs::entity ghost, const z13::gameplay::GhostPlayer& marker, const Eigen::Matrix4f& transform) {
          ++ghosts;
          EXPECT_TRUE(stored.contains(marker.id));
          EXPECT_FALSE(client.lookup(z13::gameplay::PlayerEntityName(marker.id).c_str())) << "a simulated player is also a ghost";
          EXPECT_FALSE(ghost.has<z13::flecs_tools::StateEntity>()) << "a ghost is state";
          const auto server = ServerPosition(marker.id);
          ASSERT_TRUE(server.has_value());
          EXPECT_LT((Eigen::Vector3f(transform.block<3, 1>(0, 3)) - *server).norm(), kGhostLagM);
        });
    EXPECT_EQ(client.get<StateDigests>().resyncs, 0u);
  }
  EXPECT_GT(ghosts, 0u) << "nobody walked out of anybody's presence";
}

TEST_F(GhostPosesTest, WithGhostsOffNoPoseIsSent) {
  Run(false);

  const SentTraffic& sent = session_->Server().World().get<NetSession>().Sent();
  for (const auto& [link, stats] : sent.by_link) {
    EXPECT_NE(link.channel, Channel::kPoses);
  }
  EXPECT_TRUE(Client(0).get<GhostPlayers>().by_player.empty());
}

}  // namespace
}  // namespace z13::net
