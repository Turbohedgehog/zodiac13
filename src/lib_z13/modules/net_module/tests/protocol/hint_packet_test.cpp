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

#include "../../src/hint_packet.h"

namespace z13::net {
namespace {

constexpr uint64_t kNow = 5000;
constexpr uint32_t kPlayer = 7;

HintPacket SomeHints(uint64_t tick) {
  return {.player_id = kPlayer,
          .tick = tick,
          .hints = {{.action_id = 8, .value = -4500}, {.action_id = 9, .value = 9000}}};
}

TEST(HintPacketTest, AClientsHintsRoundTripWithoutThePlayer) {
  const HintPacket sent = SomeHints(kNow + 3);

  const std::vector<uint8_t> bytes = EncodeOwnHints(sent);
  const auto received = DecodeOwnHints(bytes, kNow);

  ASSERT_TRUE(received.has_value()) << received.error();
  EXPECT_EQ(received->tick, sent.tick);
  EXPECT_EQ(received->hints, sent.hints);
  EXPECT_EQ(received->player_id, 0u) << "the server knows the player from the connection";
}

TEST(HintPacketTest, ARelayedPacketCarriesThePlayer) {
  const HintPacket sent = SomeHints(kNow - 6);

  const auto received = DecodeRelayedHints(EncodeRelayedHints(sent), kNow);

  ASSERT_TRUE(received.has_value()) << received.error();
  EXPECT_EQ(*received, sent);
}

TEST(HintPacketTest, TwoHintsTakeFarFewerBytesThanAMessageWould) {
  constexpr size_t kOwnBytes = 4 + 1 + (2 * 4);
  EXPECT_EQ(EncodeOwnHints(SomeHints(kNow)).size(), kOwnBytes);
  EXPECT_EQ(EncodeRelayedHints(SomeHints(kNow)).size(), kOwnBytes + 4);
}

TEST(HintPacketTest, TheTickIsTheNearestOneWithThoseLowBits) {
  constexpr uint64_t kFar = (uint64_t {1} << 32) * 3 + 100;
  HintPacket packet = SomeHints(kFar + 4);

  const auto ahead = DecodeOwnHints(EncodeOwnHints(packet), kFar);
  packet.tick = kFar - 4;
  const auto behind = DecodeOwnHints(EncodeOwnHints(packet), kFar);

  ASSERT_TRUE(ahead && behind);
  EXPECT_EQ(ahead->tick, kFar + 4);
  EXPECT_EQ(behind->tick, kFar - 4);
}

TEST(HintPacketTest, ATickBeforeTheStartDoesNotWrapBelowZero) {
  HintPacket packet = SomeHints(0);

  const auto received = DecodeOwnHints(EncodeOwnHints(packet), 3);

  ASSERT_TRUE(received.has_value());
  EXPECT_EQ(received->tick, 0u);
}

TEST(HintPacketTest, AMalformedPacketIsAnError) {
  std::vector<uint8_t> bytes = EncodeOwnHints(SomeHints(kNow));

  EXPECT_FALSE(DecodeOwnHints({}, kNow).has_value());
  EXPECT_FALSE(DecodeOwnHints(std::span(bytes).first(bytes.size() - 1), kNow).has_value()) << "cut short";
  bytes.push_back(0);
  EXPECT_FALSE(DecodeOwnHints(bytes, kNow).has_value()) << "longer than its count says";
  bytes = EncodeOwnHints(SomeHints(kNow));
  bytes[4] = kMaxHintsPerPacket + 1;
  EXPECT_FALSE(DecodeOwnHints(bytes, kNow).has_value()) << "more hints than a packet may hold";
}

TEST(HintPacketTest, AnOverlongPacketIsCutNotSent) {
  HintPacket packet {.player_id = kPlayer, .tick = kNow};
  packet.hints.assign(kMaxHintsPerPacket + 5, {.action_id = 1, .value = 1});

  const auto received = DecodeOwnHints(EncodeOwnHints(packet), kNow);

  ASSERT_TRUE(received.has_value());
  EXPECT_EQ(received->hints.size(), kMaxHintsPerPacket);
}

}  // namespace
}  // namespace z13::net
