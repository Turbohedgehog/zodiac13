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
#include <cstdint>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <lib_core/utils/endpoint.h>
#include <net_module/discovery_messages.h>
#include <net_module/discovery_socket.h>

// A query and its answer through real sockets on 127.0.0.1: broadcast isn't reliable in CI.
namespace z13::net {
namespace {

constexpr auto kWaitTimeout = std::chrono::seconds(5);
constexpr auto kWaitInterval = std::chrono::milliseconds(2);
constexpr uint64_t kSentAtMs = 7;

// The first datagram `socket` receives within kWaitTimeout.
std::optional<Datagram> WaitForDatagram(const DiscoverySocket& socket) {
  const auto deadline = std::chrono::steady_clock::now() + kWaitTimeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (std::vector<Datagram> received = socket.Receive(); !received.empty()) {
      return received.front();
    }
    std::this_thread::sleep_for(kWaitInterval);
  }
  return std::nullopt;
}

TEST(LanDiscoverySocketTest, AServerAnswersAQueryOverLoopback) {
  auto server = DiscoverySocket::Open(0, false);
  auto client = DiscoverySocket::Open(0, true);
  ASSERT_TRUE(server.has_value()) << server.error();
  ASSERT_TRUE(client.has_value()) << client.error();

  const Endpoint server_address {.host = std::string(kLoopbackHost), .port = server->Port()};
  ASSERT_TRUE(client->SendTo(server_address, EncodeQuery(kSentAtMs)).has_value());
  const auto query = WaitForDatagram(*server);
  ASSERT_TRUE(query.has_value());
  const auto reply = ReplyTo(query->bytes, {.name = "Loopback", .game_port = server_address.port});
  ASSERT_TRUE(reply.has_value());
  ASSERT_TRUE(server->SendTo(query->from, *reply).has_value());

  const auto answer = WaitForDatagram(*client);
  ASSERT_TRUE(answer.has_value());
  EXPECT_EQ(answer->from, server_address);
  const auto message = DecodeDiscovery(answer->bytes);
  ASSERT_TRUE(message.has_value()) << message.error();
  ASSERT_NE(message->body.AsServerInfo(), nullptr);
  EXPECT_EQ(message->body.AsServerInfo()->name, "Loopback");
  EXPECT_EQ(message->body.AsServerInfo()->sent_at_ms, kSentAtMs);
}

}  // namespace
}  // namespace z13::net
