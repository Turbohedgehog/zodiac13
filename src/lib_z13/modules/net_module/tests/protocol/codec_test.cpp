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
#include <limits>
#include <vector>

#include <net_module/protocol.h>

namespace z13::net {
namespace {

namespace fbn = fbs::net;

template <typename T>
T RoundTrip(T value) {
  Envelope envelope;
  envelope.body.Set(value);
  const std::vector<uint8_t> bytes = EncodeMessage(envelope);
  const auto decoded = DecodeMessage(bytes);
  EXPECT_TRUE(decoded.has_value()) << (decoded ? "" : decoded.error());
  if (!decoded) {
    return T {};
  }
  const T* body = AsBody<T>(decoded->body);
  EXPECT_NE(body, nullptr);
  return body ? *body : T {};
}

TEST(CodecTest, ClientHelloRoundTrips) {
  fbn::ClientHelloT hello;
  hello.version = kProtocolVersion;

  EXPECT_EQ(RoundTrip(hello).version, kProtocolVersion);
}

TEST(CodecTest, ClientHelloPreservesAMismatchedVersion) {
  // DecodeMessage never checks the version itself -- rejecting a stale/newer
  // client is session-level logic (a later branch); the codec's job is only to
  // carry whatever version the sender actually sent, unmodified.
  fbn::ClientHelloT hello;
  hello.version = kProtocolVersion + 1;

  EXPECT_EQ(RoundTrip(hello).version, kProtocolVersion + 1);
}

TEST(CodecTest, CommandBatchRoundTripsCommandsInOrder) {
  fbn::CommandBatchT batch;
  batch.base_tick = 123456789;
  batch.commands.emplace_back(3, 7, -1234);
  batch.commands.emplace_back(255, 1, 0);  // ubyte tick_delta must not overflow/truncate

  const auto decoded = RoundTrip(batch);

  EXPECT_EQ(decoded.base_tick, 123456789u);
  ASSERT_EQ(decoded.commands.size(), 2u);
  EXPECT_EQ(decoded.commands[0].tick_delta(), 3);
  EXPECT_EQ(decoded.commands[0].action_id(), 7);
  EXPECT_EQ(decoded.commands[0].value(), -1234);
  EXPECT_EQ(decoded.commands[1].tick_delta(), 255);
}

TEST(CodecTest, ResyncRequestRoundTrips) {
  RoundTrip(fbn::ResyncRequestT {});  // no fields; just proving the empty-table path works
}

TEST(CodecTest, PingRoundTrips) {
  fbn::PingT ping;
  ping.client_tick = 42;

  EXPECT_EQ(RoundTrip(ping).client_tick, 42u);
}

TEST(CodecTest, PongRoundTrips) {
  fbn::PongT pong;
  pong.client_tick = 42;
  pong.server_tick = 45;

  const auto decoded = RoundTrip(pong);
  EXPECT_EQ(decoded.client_tick, 42u);
  EXPECT_EQ(decoded.server_tick, 45u);
}

TEST(CodecTest, WelcomeRoundTripsSnapshotAndCommandLists) {
  fbn::WelcomeT welcome;
  welcome.player_id = 3;
  welcome.server_tick = 900;
  welcome.snapshot = {1, 2, 3, 4, 5};
  welcome.snapshot_tick = 840;
  welcome.actions = {6, 7, 8};
  welcome.held_values.emplace_back(0, 2, 500);
  welcome.pending.emplace_back(1, 3, -500);

  const auto decoded = RoundTrip(welcome);

  EXPECT_EQ(decoded.player_id, 3u);
  EXPECT_EQ(decoded.server_tick, 900u);
  EXPECT_EQ(decoded.snapshot, welcome.snapshot);
  EXPECT_EQ(decoded.snapshot_tick, 840u);
  EXPECT_EQ(decoded.actions, welcome.actions);
  ASSERT_EQ(decoded.held_values.size(), 1u);
  EXPECT_EQ(decoded.held_values[0].action_id(), 2);
  ASSERT_EQ(decoded.pending.size(), 1u);
  EXPECT_EQ(decoded.pending[0].value(), -500);
}

TEST(CodecTest, RejectedRoundTripsReason) {
  fbn::RejectedT rejected;
  rejected.reason = "protocol version mismatch";

  EXPECT_EQ(RoundTrip(rejected).reason, "protocol version mismatch");
}

TEST(CodecTest, ResyncRoundTrips) {
  fbn::ResyncT resync;
  resync.server_tick = 1000;
  resync.snapshot = {9, 8, 7};

  const auto decoded = RoundTrip(resync);
  EXPECT_EQ(decoded.server_tick, 1000u);
  EXPECT_EQ(decoded.snapshot, resync.snapshot);
}

TEST(CodecTest, PlayerJoinedRoundTrips) {
  fbn::PlayerJoinedT joined;
  joined.apply_tick = 55;
  joined.entity_state = {1, 1, 2, 3};

  const auto decoded = RoundTrip(joined);
  EXPECT_EQ(decoded.apply_tick, 55u);
  EXPECT_EQ(decoded.entity_state, joined.entity_state);
}

TEST(CodecTest, PlayerLeftRoundTrips) {
  fbn::PlayerLeftT left;
  left.apply_tick = 56;
  left.player_id = 2;

  const auto decoded = RoundTrip(left);
  EXPECT_EQ(decoded.apply_tick, 56u);
  EXPECT_EQ(decoded.player_id, 2u);
}

TEST(CodecTest, SequencedCommandsRoundTrips) {
  fbn::SequencedCommandsT sequenced;
  sequenced.player_id = 1;
  sequenced.base_tick = 777;
  sequenced.commands.emplace_back(0, 4, 100);

  const auto decoded = RoundTrip(sequenced);
  EXPECT_EQ(decoded.player_id, 1u);
  EXPECT_EQ(decoded.base_tick, 777u);
  ASSERT_EQ(decoded.commands.size(), 1u);
  EXPECT_EQ(decoded.commands[0].action_id(), 4);
}

TEST(CodecTest, StateDigestRoundTripsPositions) {
  fbn::StateDigestT digest;
  digest.tick = 60;
  digest.entity_count = 12;
  digest.name_hash = 0xDEADBEEF;
  digest.positions.emplace_back(1, 1.5f, 2.5f, 3.5f);

  const auto decoded = RoundTrip(digest);
  EXPECT_EQ(decoded.tick, 60u);
  EXPECT_EQ(decoded.entity_count, 12u);
  EXPECT_EQ(decoded.name_hash, 0xDEADBEEFu);
  ASSERT_EQ(decoded.positions.size(), 1u);
  EXPECT_EQ(decoded.positions[0].player_id(), 1u);
  EXPECT_FLOAT_EQ(decoded.positions[0].y(), 2.5f);
}

TEST(CodecTest, TruncatedBytesAreRejectedNotCrashed) {
  fbn::PingT ping;
  ping.client_tick = 7;
  Envelope envelope;
  envelope.body.Set(ping);
  const auto bytes = EncodeMessage(envelope);
  for (size_t len = 0; len < bytes.size(); ++len) {
    const auto decoded = DecodeMessage(std::span(bytes).first(len));
    EXPECT_FALSE(decoded.has_value()) << "length " << len << " should not verify";
  }
}

TEST(CodecTest, GarbageBytesAreRejected) {
  const std::vector<uint8_t> garbage(64, 0xAB);
  EXPECT_FALSE(DecodeMessage(garbage).has_value());
}

TEST(CodecTest, EmptyBytesAreRejected) {
  EXPECT_FALSE(DecodeMessage(std::span<const uint8_t> {}).has_value());
}

TEST(QuantizeActionValueTest, RoundTripsWithinOneQuantizationStep) {
  for (const float value : {0.f, 1.f, -1.f, 0.5f, -0.33f, 3.14f}) {
    const float dequantized = DequantizeActionValue(QuantizeActionValue(value));
    EXPECT_NEAR(dequantized, value, 1.f / kActionValueScale);
  }
}

TEST(QuantizeActionValueTest, BooleanActionsRoundTripExactly) {
  EXPECT_EQ(QuantizeActionValue(0.f), 0);
  EXPECT_EQ(DequantizeActionValue(QuantizeActionValue(1.f)), 1.f);
}

TEST(QuantizeActionValueTest, OutOfRangeValuesClampInsteadOfOverflowing) {
  const int16_t max_wire = std::numeric_limits<int16_t>::max();
  const int16_t min_wire = std::numeric_limits<int16_t>::min();

  EXPECT_EQ(QuantizeActionValue(1e9f), max_wire);
  EXPECT_EQ(QuantizeActionValue(-1e9f), min_wire);
}

}  // namespace
}  // namespace z13::net
