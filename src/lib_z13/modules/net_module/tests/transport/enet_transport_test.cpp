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

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <lib_core/endpoint.h>
#include <net_module/enet_transport.h>

// Real ENet over loopback, no mocking -- see docs/client-server-plan.md's stage-2
// scope. Each test uses its own port so they don't race each other; timing-bound
// assertions use generous bounds since this exercises the real OS network stack.
namespace z13::net {
namespace {

constexpr auto kPumpTimeout = std::chrono::seconds(5);
constexpr auto kPumpInterval = std::chrono::milliseconds(2);

using EventLog = std::vector<TransportEvent>;

std::vector<std::byte> ToBytes(std::string_view text) {
  const auto* begin = reinterpret_cast<const std::byte*>(text.data());
  return std::vector<std::byte>(begin, begin + text.size());
}

std::string ToString(const std::vector<std::byte>& bytes) {
  return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

std::unique_ptr<Transport> MustCreateServer(uint16_t port, size_t max_clients = kMaxClients) {
  auto result = CreateEnetServerTransport(port, max_clients);
  EXPECT_TRUE(result.has_value()) << (result ? "" : result.error());
  return result ? std::move(*result) : nullptr;
}

std::unique_ptr<Transport> MustCreateClient(const Endpoint& server) {
  auto result = CreateEnetClientTransport(server);
  EXPECT_TRUE(result.has_value()) << (result ? "" : result.error());
  return result ? std::move(*result) : nullptr;
}

// Services every (transport, log) pair, appending new events into that transport's
// own log, until `condition` is true or kPumpTimeout elapses. Real sockets need
// actual OS scheduling turns, not a fixed tick count (contrast InMemoryTransport's
// deterministic Tick()).
bool PumpUntil(
    std::vector<std::pair<Transport*, EventLog*>> transports, const std::function<bool()>& condition) {
  const auto deadline = std::chrono::steady_clock::now() + kPumpTimeout;
  do {
    for (auto& [transport, log] : transports) {
      auto pumped = transport->Service();
      log->insert(log->end(), std::make_move_iterator(pumped.begin()), std::make_move_iterator(pumped.end()));
    }
    if (condition()) {
      return true;
    }
    std::this_thread::sleep_for(kPumpInterval);
  } while (std::chrono::steady_clock::now() < deadline);
  return condition();
}

TEST(EnetTransportTest, ConnectAndExchangeReliableDataBothWays) {
  constexpr uint16_t kPort = 27001;
  auto server = MustCreateServer(kPort);
  auto client = MustCreateClient({.host = "127.0.0.1", .port = kPort});
  ASSERT_TRUE(server);
  ASSERT_TRUE(client);

  EventLog server_log;
  EventLog client_log;
  ASSERT_TRUE(PumpUntil(
      {{server.get(), &server_log}, {client.get(), &client_log}},
      [&] { return !server_log.empty() && !client_log.empty(); }));
  ASSERT_FALSE(server_log.empty());
  ASSERT_FALSE(client_log.empty());
  EXPECT_EQ(server_log.front().kind, TransportEventKind::kConnected);
  EXPECT_EQ(client_log.front().kind, TransportEventKind::kConnected);
  const ConnectionId server_side_client = server_log.front().connection;
  const ConnectionId client_side_server = client_log.front().connection;
  server_log.clear();
  client_log.clear();

  client->Send(client_side_server, Channel::kReliable, ToBytes("hi server"));
  server->Send(server_side_client, Channel::kReliable, ToBytes("hi client"));

  ASSERT_TRUE(PumpUntil(
      {{server.get(), &server_log}, {client.get(), &client_log}},
      [&] { return !server_log.empty() && !client_log.empty(); }));
  ASSERT_FALSE(server_log.empty());
  ASSERT_FALSE(client_log.empty());
  EXPECT_EQ(ToString(server_log.front().data), "hi server");
  EXPECT_EQ(ToString(client_log.front().data), "hi client");
}

TEST(EnetTransportTest, CleanDisconnectNotifiesTheOtherSide) {
  constexpr uint16_t kPort = 27002;
  auto server = MustCreateServer(kPort);
  auto client = MustCreateClient({.host = "127.0.0.1", .port = kPort});
  ASSERT_TRUE(server);
  ASSERT_TRUE(client);

  EventLog server_log;
  EventLog client_log;
  ASSERT_TRUE(PumpUntil(
      {{server.get(), &server_log}, {client.get(), &client_log}},
      [&] { return !server_log.empty() && !client_log.empty(); }));
  const ConnectionId client_side_server = client_log.front().connection;
  server_log.clear();

  client->Disconnect(client_side_server);

  // enet_peer_disconnect() only queues the disconnect command; it's only actually
  // sent on the wire inside enet_host_service(), so the client transport must keep
  // being serviced here too, not just the server that's waiting for the event.
  ASSERT_TRUE(PumpUntil(
      {{server.get(), &server_log}, {client.get(), &client_log}}, [&] { return !server_log.empty(); }));
  EXPECT_EQ(server_log.front().kind, TransportEventKind::kDisconnected);
}

TEST(EnetTransportTest, ConnectingToAClosedPortFailsWithinBoundedTime) {
  constexpr uint16_t kClosedPort = 27003;  // nothing listens here
  auto client = MustCreateClient({.host = "127.0.0.1", .port = kClosedPort});
  ASSERT_TRUE(client);

  EventLog client_log;
  ASSERT_TRUE(PumpUntil({{client.get(), &client_log}}, [&] { return !client_log.empty(); }));
  EXPECT_EQ(client_log.front().kind, TransportEventKind::kDisconnected);
}

TEST(EnetTransportTest, BindingAnAlreadyBoundPortFailsToStart) {
  constexpr uint16_t kPort = 27004;
  auto first = MustCreateServer(kPort);
  ASSERT_TRUE(first);

  EXPECT_FALSE(CreateEnetServerTransport(kPort).has_value());
}

TEST(EnetTransportTest, ConnectionsBeyondMaxClientsAreRejected) {
  constexpr uint16_t kPort = 27005;
  constexpr size_t kMax = 1;
  auto server = MustCreateServer(kPort, kMax);
  auto first_client = MustCreateClient({.host = "127.0.0.1", .port = kPort});
  auto second_client = MustCreateClient({.host = "127.0.0.1", .port = kPort});
  ASSERT_TRUE(server);
  ASSERT_TRUE(first_client);
  ASSERT_TRUE(second_client);

  EventLog server_log;
  EventLog first_log;
  EventLog second_log;
  ASSERT_TRUE(PumpUntil(
      {{server.get(), &server_log}, {first_client.get(), &first_log}}, [&] { return !first_log.empty(); }));
  EXPECT_EQ(first_log.front().kind, TransportEventKind::kConnected);

  // The server is already full; the second client should time out, not connect.
  ASSERT_TRUE(PumpUntil({{second_client.get(), &second_log}}, [&] { return !second_log.empty(); }));
  EXPECT_EQ(second_log.front().kind, TransportEventKind::kDisconnected);
}

TEST(EnetTransportTest, CreatingAndDestroyingManyHostsDoesNotLeak) {
  constexpr uint16_t kPort = 27006;
  constexpr int kIterations = 20;
  for (int i = 0; i < kIterations; ++i) {
    auto server = MustCreateServer(kPort);
    ASSERT_TRUE(server) << "iteration " << i;
    auto client = MustCreateClient({.host = "127.0.0.1", .port = kPort});
    ASSERT_TRUE(client) << "iteration " << i;

    EventLog server_log;
    EventLog client_log;
    ASSERT_TRUE(PumpUntil(
        {{server.get(), &server_log}, {client.get(), &client_log}},
        [&] { return !server_log.empty() && !client_log.empty(); }))
        << "iteration " << i;
    // server/client destructors run here (enet_host_destroy) before the next
    // iteration rebinds the same port -- a leaked socket/descriptor would make a
    // later iteration's bind fail well before kIterations.
  }
}

}  // namespace
}  // namespace z13::net
