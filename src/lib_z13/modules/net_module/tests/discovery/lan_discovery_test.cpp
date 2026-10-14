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
#include <string>
#include <vector>

#include <lib_core/utils/endpoint.h>
#include <net_module/discovery_messages.h>
#include <net_module/protocol.h>
#include <net_module/reply_limiter.h>
#include <net_module/server_finder.h>

// Finding servers on the LAN without sockets (docs/server-plan.md, f/lan-discovery).
namespace z13::net {
namespace {

constexpr uint64_t kSentAtMs = 1000;
constexpr uint64_t kPingMs = 30;
constexpr uint64_t kReplyIntervalMs = 200;
constexpr uint64_t kForgetAfterMs = 5000;
constexpr uint16_t kGamePort = 26213;
const Endpoint kClient {.host = "192.168.1.7", .port = 40000};
const Endpoint kOtherClient {.host = "192.168.1.8", .port = 40000};
const Endpoint kServerAddress {.host = "192.168.1.5", .port = 26214};

ServerInfo Info() {
  return {.name = "Hub", .protocol_version = kProtocolVersion, .scene = "spire", .players = 3, .game_port = kGamePort};
}

TEST(LanDiscoveryTest, AQueryGetsTheServersInfoWithItsTime) {
  const auto reply = ReplyTo(EncodeQuery(kSentAtMs), Info());
  ASSERT_TRUE(reply.has_value());
  const auto message = DecodeDiscovery(*reply);
  ASSERT_TRUE(message.has_value()) << message.error();
  const ServerInfo* info = message->body.AsServerInfo();
  ASSERT_NE(info, nullptr);
  EXPECT_EQ(info->sent_at_ms, kSentAtMs);
  EXPECT_EQ(info->name, "Hub");
  EXPECT_EQ(info->scene, "spire");
  EXPECT_EQ(info->game_port, kGamePort);
}

TEST(LanDiscoveryTest, OtherBytesFormatsAndKindsGetNoReply) {
  const std::vector<uint8_t> garbage = {1, 2, 3, 4, 5, 6, 7, 8};
  EXPECT_FALSE(ReplyTo(garbage, Info()).has_value());

  DiscoveryMessage newer;
  newer.body.Set(fbs::discovery::DiscoveryQueryT {.format_version = kDiscoveryFormatVersion + 1, .sent_at_ms = 1});
  EXPECT_FALSE(ReplyTo(EncodeDiscovery(newer), Info()).has_value());

  DiscoveryMessage answer;
  answer.body.Set(Info());
  EXPECT_FALSE(ReplyTo(EncodeDiscovery(answer), Info()).has_value());
}

TEST(LanDiscoveryTest, AnAddressIsAnsweredOnceAnInterval) {
  ReplyLimiter limiter;
  EXPECT_TRUE(limiter.Allow(kClient, 0, kReplyIntervalMs));
  EXPECT_FALSE(limiter.Allow(kClient, kReplyIntervalMs - 1, kReplyIntervalMs));
  EXPECT_TRUE(limiter.Allow(kOtherClient, kReplyIntervalMs - 1, kReplyIntervalMs));
  EXPECT_TRUE(limiter.Allow(kClient, kReplyIntervalMs, kReplyIntervalMs));
}

TEST(LanDiscoveryTest, AFoundServerIsJoinedAtItsGamePortAndForgottenWhenSilent) {
  ServerFinder finder;
  ServerInfo info = Info();
  info.sent_at_ms = kSentAtMs;
  finder.Record(kServerAddress, info, kSentAtMs + kPingMs);
  ASSERT_EQ(finder.Servers().size(), 1U);
  const FoundServer& found = finder.Servers().front();
  EXPECT_EQ(found.endpoint, (Endpoint {.host = kServerAddress.host, .port = kGamePort}));
  EXPECT_EQ(found.ping_ms, kPingMs);
  EXPECT_EQ(found.scene, "spire");
  EXPECT_FALSE(found.incompatible.has_value());

  finder.Forget(kSentAtMs + kForgetAfterMs - 1, kForgetAfterMs);
  EXPECT_EQ(finder.Servers().size(), 1U);
  finder.Forget(kSentAtMs + kPingMs + kForgetAfterMs, kForgetAfterMs);
  EXPECT_TRUE(finder.Servers().empty());
}

TEST(LanDiscoveryTest, AServerOfAnotherProtocolIsListedAsIncompatible) {
  ServerFinder finder;
  ServerInfo info = Info();
  info.protocol_version = kProtocolVersion + 1;
  finder.Record(kServerAddress, info, kSentAtMs);
  ASSERT_EQ(finder.Servers().size(), 1U);
  EXPECT_TRUE(finder.Servers().front().incompatible.has_value());
}

}  // namespace
}  // namespace z13::net
