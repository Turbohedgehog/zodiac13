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

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include <lib_core/settings/config.h>
#include <net_module/in_memory_transport.h>
#include <net_module/protocol.h>

#include "../../src/net_session.h"

// QueueRun holds the runs of a flush into one message, and keeps them in order with the other reliable messages.
namespace z13::net {
namespace {

namespace fbn = fbs::net;

constexpr uint16_t kPort = kDefaultServerPort;
constexpr uint32_t kFirstPlayer = 3;
constexpr uint32_t kSecondPlayer = 4;
constexpr uint64_t kPingTick = 99;

class RelayPackingTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto transport = CreateInMemoryServerTransport(network_, kPort);
    ASSERT_TRUE(transport.has_value());
    server_.Open(std::move(*transport));
    client_ = CreateInMemoryClientTransport(network_, kPort);
    network_.Tick();
    for (const TransportEvent& event : server_.Service()) {
      if (event.kind == TransportEventKind::kConnected) {
        connection_ = event.connection;
        server_.AddConnection(event.connection);
      }
    }
    ASSERT_NE(connection_, kInvalidConnectionId);
    client_->Service();
  }

  std::vector<Envelope> Received() {
    network_.Tick();
    std::vector<Envelope> messages;
    for (const TransportEvent& event : client_->Service()) {
      if (event.kind == TransportEventKind::kReceived) {
        auto decoded = DecodeMessage({reinterpret_cast<const uint8_t*>(event.data.data()), event.data.size()});
        EXPECT_TRUE(decoded.has_value());
        if (decoded) {
          messages.push_back(std::move(*decoded));
        }
      }
    }
    return messages;
  }

  void QueueRun(uint32_t player_id, std::vector<fbn::CommandWire> commands) {
    const std::array connections = {connection_};
    server_.QueueRun(connections, {10, 12, player_id, static_cast<uint32_t>(commands.size())}, commands);
  }

  InMemoryNetwork network_;
  NetSession server_;
  std::unique_ptr<Transport> client_;
  ConnectionId connection_ = kInvalidConnectionId;
};

TEST_F(RelayPackingTest, RunsQueuedBeforeAFlushArriveInOneMessage) {
  QueueRun(kFirstPlayer, {{0, 1, 100}, {1, 2, 200}});
  QueueRun(kSecondPlayer, {{0, 3, 300}});
  EXPECT_TRUE(Received().empty()) << "a run was sent before the flush";

  server_.FlushRelays();
  const std::vector<Envelope> received = Received();

  ASSERT_EQ(received.size(), 1u);
  const auto* sequenced = received[0].body.AsSequencedCommands();
  ASSERT_NE(sequenced, nullptr);
  ASSERT_EQ(sequenced->runs.size(), 2u);
  EXPECT_EQ(sequenced->runs[0].player_id(), kFirstPlayer);
  EXPECT_EQ(sequenced->runs[0].count(), 2u);
  EXPECT_EQ(sequenced->runs[1].player_id(), kSecondPlayer);
  ASSERT_EQ(sequenced->commands.size(), 3u);
  EXPECT_EQ(sequenced->commands[2].action_id(), 3);
}

TEST_F(RelayPackingTest, AnotherReliableMessageFollowsTheRunsQueuedBeforeIt) {
  QueueRun(kFirstPlayer, {{0, 1, 100}});
  Envelope ping;
  fbn::PingT body;
  body.client_tick = kPingTick;
  ping.body.Set(std::move(body));

  server_.Send(connection_, Channel::kReliable, ping);
  const std::vector<Envelope> received = Received();

  ASSERT_EQ(received.size(), 2u);
  EXPECT_NE(received[0].body.AsSequencedCommands(), nullptr);
  EXPECT_NE(received[1].body.AsPing(), nullptr);
  server_.FlushRelays();
  EXPECT_TRUE(Received().empty()) << "a flushed run was sent again";
}

}  // namespace
}  // namespace z13::net
