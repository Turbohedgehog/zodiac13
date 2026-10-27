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

#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

#include "../../src/pose_packet.h"

namespace z13::net {
namespace {

constexpr uint64_t kNow = 5000;
constexpr float kAngleStepDeg = 0.01f;

PoseRecord Pose(uint32_t player_id, float x, float yaw_deg, float pitch_deg) {
  return {.player_id = player_id, .position = {x, -2.5f, 31.25f}, .look = {.yaw_deg = yaw_deg, .pitch_deg = pitch_deg}};
}

TEST(PosePacketTest, PosesRoundTripWithTheAnglesInHundredthsOfADegree) {
  const PosePacket packet {.tick = kNow - 3, .poses = {Pose(7, 10.5f, -179.99f, 45.f), Pose(9, -4.f, 90.f, -88.5f)}};

  const auto decoded = DecodePoses(EncodePoses(packet), kNow);

  ASSERT_TRUE(decoded.has_value()) << decoded.error();
  EXPECT_EQ(decoded->tick, packet.tick);
  ASSERT_EQ(decoded->poses.size(), 2u);
  EXPECT_EQ(decoded->poses[0].player_id, 7u);
  EXPECT_EQ(decoded->poses[0].position, packet.poses[0].position);
  EXPECT_NEAR(decoded->poses[0].look.yaw_deg, -179.99f, kAngleStepDeg);
  EXPECT_NEAR(decoded->poses[1].look.pitch_deg, -88.5f, kAngleStepDeg);
}

TEST(PosePacketTest, AListLongerThanAPacketIsCut) {
  PosePacket packet {.tick = kNow};
  for (uint32_t id = 0; id < kMaxPosesPerPacket + 5; ++id) {
    packet.poses.push_back(Pose(id, 0.f, 0.f, 0.f));
  }

  const auto decoded = DecodePoses(EncodePoses(packet), kNow);

  ASSERT_TRUE(decoded.has_value());
  EXPECT_EQ(decoded->poses.size(), kMaxPosesPerPacket);
}

TEST(PosePacketTest, AnInconsistentPacketIsRefused) {
  std::vector<uint8_t> bytes = EncodePoses({.tick = kNow, .poses = {Pose(1, 0.f, 0.f, 0.f)}});

  bytes.pop_back();

  EXPECT_FALSE(DecodePoses(bytes, kNow).has_value());
  EXPECT_FALSE(DecodePoses({}, kNow).has_value());
}

TEST(PosePacketTest, APoseThatIsNotAPlaceIsRefused) {
  PoseRecord pose = Pose(1, 0.f, 0.f, 0.f);
  pose.position.y() = std::numeric_limits<float>::quiet_NaN();

  EXPECT_FALSE(DecodePoses(EncodePoses({.tick = kNow, .poses = {pose}}), kNow).has_value());
}

}  // namespace
}  // namespace z13::net
