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

#include <lib_core/settings/config.h>

#include <net_module/in_memory_transport.h>

#include "../../../net_module/src/net_session.h"
#include "../support/load_session.h"

namespace z13::net {
namespace {

using fbs::net::MessageBody;
using z13::testing::LoadSession;

constexpr size_t kClients = 3;
constexpr uint64_t kConnectTicks = 600;
constexpr uint64_t kLoadedTicks = 120;

class ServerTrafficTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(session_.Connect(kConnectTicks));
    for (uint64_t tick = 0; tick < kLoadedTicks; ++tick) {
      session_.Tick();
    }
  }

  LoadSession session_ {kClients};
};

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
