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

#include <memory>
#include <string>
#include <string_view>

#include <lib_core/config.h>
#include <net_module/in_memory_transport.h>

namespace z13::net {
namespace {

constexpr uint16_t kPort = kDefaultServerPort;

std::vector<std::byte> ToBytes(std::string_view text) {
  const auto* begin = reinterpret_cast<const std::byte*>(text.data());
  return std::vector<std::byte>(begin, begin + text.size());
}

std::string ToString(const std::vector<std::byte>& bytes) {
  return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

std::unique_ptr<Transport> MustCreateServer(InMemoryNetwork& network, uint16_t port) {
  auto result = CreateInMemoryServerTransport(network, port);
  EXPECT_TRUE(result.has_value()) << (result ? "" : result.error());
  return result ? std::move(*result) : nullptr;
}

TEST(InMemoryTransportTest, ClientConnectsToServerListeningOnThatPort) {
  InMemoryNetwork network;
  auto server = MustCreateServer(network, kPort);
  auto client = CreateInMemoryClientTransport(network, kPort);

  network.Tick();

  const auto server_events = server->Service();
  const auto client_events = client->Service();
  ASSERT_EQ(server_events.size(), 1u);
  EXPECT_EQ(server_events[0].kind, TransportEventKind::kConnected);
  ASSERT_EQ(client_events.size(), 1u);
  EXPECT_EQ(client_events[0].kind, TransportEventKind::kConnected);
}

TEST(InMemoryTransportTest, ConnectingToAPortWithNoListenerDisconnects) {
  InMemoryNetwork network;
  auto client = CreateInMemoryClientTransport(network, kPort);

  network.Tick();

  const auto events = client->Service();
  ASSERT_EQ(events.size(), 1u);
  EXPECT_EQ(events[0].kind, TransportEventKind::kDisconnected);
}

TEST(InMemoryTransportTest, SendReceiveRoundTripsOnBothChannelsInOrder) {
  InMemoryNetwork network;
  auto server = MustCreateServer(network, kPort);
  auto client = CreateInMemoryClientTransport(network, kPort);
  network.Tick();
  const ConnectionId client_side_server = client->Service().at(0).connection;
  server->Service();

  client->Send(client_side_server, Channel::kReliable, ToBytes("hello"));
  client->Send(client_side_server, Channel::kUnreliable, ToBytes("ping"));
  network.Tick();

  const auto events = server->Service();
  ASSERT_EQ(events.size(), 2u);
  EXPECT_EQ(events[0].channel, Channel::kReliable);
  EXPECT_EQ(ToString(events[0].data), "hello");
  EXPECT_EQ(events[1].channel, Channel::kUnreliable);
  EXPECT_EQ(ToString(events[1].data), "ping");
}

TEST(InMemoryTransportTest, DisconnectNotifiesTheOtherSide) {
  InMemoryNetwork network;
  auto server = MustCreateServer(network, kPort);
  auto client = CreateInMemoryClientTransport(network, kPort);
  network.Tick();
  const ConnectionId client_side_server = client->Service().at(0).connection;
  server->Service();

  client->Disconnect(client_side_server);
  network.Tick();

  const auto events = server->Service();
  ASSERT_EQ(events.size(), 1u);
  EXPECT_EQ(events[0].kind, TransportEventKind::kDisconnected);
}

TEST(InMemoryTransportTest, SecondListenerOnTheSamePortIsRejected) {
  InMemoryNetwork network;
  auto server = MustCreateServer(network, kPort);

  EXPECT_FALSE(CreateInMemoryServerTransport(network, kPort).has_value());
}

TEST(InMemoryTransportTest, PortIsReusableOnceTheFirstListenerIsDestroyed) {
  InMemoryNetwork network;
  { auto server = MustCreateServer(network, kPort); }

  EXPECT_TRUE(CreateInMemoryServerTransport(network, kPort).has_value());
}

TEST(InMemoryTransportTest, FaultConfigDelaysDeliveryByExactlyTheConfiguredTicks) {
  InMemoryNetwork network;
  network.SetFaultConfig({.min_delay_ticks = 3, .max_delay_ticks = 3});
  auto server = MustCreateServer(network, kPort);
  auto client = CreateInMemoryClientTransport(network, kPort);

  network.Tick();
  network.Tick();
  EXPECT_TRUE(client->Service().empty()) << "connect should not complete before the configured delay";
  network.Tick();
  EXPECT_FALSE(client->Service().empty());
}

// Mirrors ENet: a reliable packet is retransmitted rather than lost, so only the
// unreliable channel can actually drop.
TEST(InMemoryTransportTest, FaultConfigDropsUnreliablePacketsAndKeepsReliableOnes) {
  InMemoryNetwork network(/*seed=*/7);
  network.SetFaultConfig({.drop_probability = 1.0});
  auto server = MustCreateServer(network, kPort);
  auto client = CreateInMemoryClientTransport(network, kPort);
  network.Tick();  // the connect handshake itself isn't subject to drop_probability
  const ConnectionId client_side_server = client->Service().at(0).connection;
  server->Service();

  client->Send(client_side_server, Channel::kUnreliable, ToBytes("lost"));
  network.Tick();
  EXPECT_TRUE(server->Service().empty());

  client->Send(client_side_server, Channel::kReliable, ToBytes("kept"));
  network.Tick();
  EXPECT_EQ(server->Service().size(), 1u);
}

TEST(InMemoryTransportTest, DestroyingATransportLeavesPendingDeliveriesHarmless) {
  InMemoryNetwork network;
  auto server = MustCreateServer(network, kPort);
  auto client = CreateInMemoryClientTransport(network, kPort);
  network.Tick();
  const ConnectionId client_side_server = client->Service().at(0).connection;
  server->Service();

  client->Send(client_side_server, Channel::kReliable, ToBytes("orphaned"));
  server.reset();

  EXPECT_NO_FATAL_FAILURE(network.Tick());
}

}  // namespace
}  // namespace z13::net
