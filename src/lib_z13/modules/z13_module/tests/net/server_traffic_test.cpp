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
#include <ranges>
#include <utility>

#include <lib_core/settings/config.h>
#include <lib_core/time/simulation_clock.h>

#include <net_module/in_memory_transport.h>
#include <z13/components/player.h>
#include <z13/components/player_action.h>

#include "../../../net_module/src/net_session.h"
#include "../support/load_session.h"

namespace z13::net {
namespace {

using fbs::net::MessageBody;
using z13::testing::LoadSession;

constexpr size_t kClients = 3;
constexpr uint64_t kConnectTicks = 600;
constexpr uint64_t kLoadedTicks = 120;
constexpr uint64_t kDeliveryTicks = 5;
constexpr uint32_t kFirstFakePlayer = 900;
constexpr uint32_t kSecondFakePlayer = 901;
constexpr uint64_t kConfirmedAhead = 3;

class ServerTrafficTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(session_.Connect(kConnectTicks));
    for (uint64_t tick = 0; tick < kLoadedTicks; ++tick) {
      session_.Tick();
    }
  }

  // Sends client 0 a SequencedCommands of two runs, one command each, whose headers claim `counted` commands in all.
  void SendTwoRuns(uint32_t counted) {
    const uint64_t through = session_.Client(0).World().get<z13::flecs_tools::SimulationClock>().tick + kConfirmedAhead;
    fbs::net::SequencedCommandsT sequenced;
    sequenced.runs.emplace_back(through, through, kFirstFakePlayer, counted - 1);
    sequenced.runs.emplace_back(through, through, kSecondFakePlayer, 1);
    sequenced.commands.emplace_back(0, 1, 100);
    sequenced.commands.emplace_back(0, 2, 100);
    Envelope envelope;
    envelope.body.Set(std::move(sequenced));
    auto& server = session_.Server().World().get_mut<NetSession>();
    const auto& connections = server.PlayerConnections();
    const uint32_t client_player = *session_.Client(0).World().get<z13::gameplay::LocalPlayer>().id;
    const auto client = std::ranges::find(connections, client_player, &std::pair<ConnectionId, uint32_t>::second);
    ASSERT_NE(client, connections.end());
    server.Send(client->first, Channel::kReliable, envelope);
    for (uint64_t tick = 0; tick < kDeliveryTicks; ++tick) {
      session_.Tick();
    }
  }

  const auto& ClientConfirmed() {
    return session_.Client(0).World().get<z13::gameplay::ConfirmedInputTicks>().by_player;
  }

  LoadSession session_ {kClients};
};

TEST_F(ServerTrafficTest, ARelayedMessageWithTwoRunsConfirmsBothPlayers) {
  SendTwoRuns(2);

  EXPECT_TRUE(ClientConfirmed().contains(kFirstFakePlayer));
  EXPECT_TRUE(ClientConfirmed().contains(kSecondFakePlayer));
}

TEST_F(ServerTrafficTest, ARelayedMessageWhoseRunsCountTooManyCommandsIsDropped) {
  SendTwoRuns(3);

  EXPECT_FALSE(ClientConfirmed().contains(kFirstFakePlayer));
  EXPECT_FALSE(ClientConfirmed().contains(kSecondFakePlayer));
}

TEST_F(ServerTrafficTest, ClientCountersAddUpToWhatTheServerReceived) {
  uint64_t counted = 0;
  for (size_t i = 0; i < session_.ClientCount(); ++i) {
    counted += session_.Client(i).World().get<NetSession>().Sent().Total().bytes;
  }

  EXPECT_GT(counted, 0u);
  EXPECT_EQ(counted, session_.Network().TrafficTo(kDefaultServerPort).bytes);
}

TEST_F(ServerTrafficTest, TheServerCountsEachPeerByChannelAndMessage) {
  const NetSession& server = session_.Server().World().get<NetSession>();
  const SentTraffic& sent = server.Sent();

  for (const auto& [connection, player_id] : server.PlayerConnections()) {
    const auto reliable = sent.by_link.find({connection, Channel::kReliable});
    ASSERT_NE(reliable, sent.by_link.end()) << "nothing counted for player " << player_id;
    EXPECT_GT(reliable->second.bytes, 0u);
  }

  const uint64_t hint_bytes = std::ranges::fold_left(
      sent.by_link | std::views::filter([](const auto& entry) { return entry.first.channel == Channel::kHints; }) |
          std::views::values | std::views::transform(&TrafficStats::bytes),
      uint64_t {}, std::plus<> {});
  const uint64_t message_bytes = std::ranges::fold_left(
      sent.by_message | std::views::values | std::views::transform(&TrafficStats::bytes), uint64_t {}, std::plus<> {});
  EXPECT_GT(hint_bytes, 0u);
  EXPECT_EQ(sent.Total().bytes, message_bytes + hint_bytes);
  EXPECT_TRUE(sent.by_message.contains(MessageBody::SequencedCommands));
}

}  // namespace
}  // namespace z13::net
