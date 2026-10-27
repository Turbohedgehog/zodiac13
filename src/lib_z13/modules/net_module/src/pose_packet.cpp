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

#include "pose_packet.h"

#include <algorithm>
#include <bit>
#include <format>
#include <ranges>

#include <z13/components/player_action.h>

#include "byte_codec.h"

namespace z13::net {

namespace {

constexpr size_t kTickBytes = sizeof(uint32_t);
constexpr size_t kCountBytes = sizeof(uint8_t);
constexpr size_t kHeaderBytes = kTickBytes + kCountBytes;
constexpr size_t kCoordinates = 3;
constexpr size_t kAngles = 2;
constexpr size_t kRecordBytes = sizeof(uint32_t) + kCoordinates * sizeof(float) + kAngles * sizeof(int16_t);

void PutFloat(std::vector<uint8_t>& out, float value) {
  Put(out, std::bit_cast<uint32_t>(value));
}

float GetFloat(std::span<const uint8_t> bytes, size_t at) {
  return std::bit_cast<float>(Get<uint32_t>(bytes, at));
}

}  // namespace

std::vector<uint8_t> EncodePoses(const PosePacket& packet) {
  const size_t count = std::min(packet.poses.size(), kMaxPosesPerPacket);
  std::vector<uint8_t> out;
  out.reserve(kHeaderBytes + count * kRecordBytes);
  Put(out, static_cast<uint32_t>(packet.tick));
  out.push_back(static_cast<uint8_t>(count));
  for (const PoseRecord& pose : packet.poses | std::views::take(count)) {
    Put(out, pose.player_id);
    for (const float coordinate : pose.position) {
      PutFloat(out, coordinate);
    }
    Put(out, static_cast<uint16_t>(z13::gameplay::QuantizeActionValue(pose.look.yaw_deg)));
    Put(out, static_cast<uint16_t>(z13::gameplay::QuantizeActionValue(pose.look.pitch_deg)));
  }
  return out;
}

std::expected<PosePacket, std::string> DecodePoses(std::span<const uint8_t> bytes, uint64_t now) {
  if (bytes.size() < kHeaderBytes) {
    return std::unexpected(std::format("a pose packet of {} bytes is shorter than its header", bytes.size()));
  }
  PosePacket packet {.tick = ResolveTick(now, Get<uint32_t>(bytes, 0))};
  const size_t count = bytes[kTickBytes];
  if (count > kMaxPosesPerPacket || bytes.size() != kHeaderBytes + count * kRecordBytes) {
    return std::unexpected(std::format("a pose packet of {} bytes does not hold {} poses", bytes.size(), count));
  }
  size_t at = kHeaderBytes;
  for (size_t i = 0; i < count; ++i, at += kRecordBytes) {
    PoseRecord pose {.player_id = Get<uint32_t>(bytes, at)};
    for (size_t axis = 0; axis < kCoordinates; ++axis) {
      pose.position[static_cast<Eigen::Index>(axis)] = GetFloat(bytes, at + sizeof(uint32_t) + axis * sizeof(float));
    }
    if (!pose.position.allFinite()) {
      return std::unexpected(std::format("a pose of player {} is not a place", pose.player_id));
    }
    const size_t angles = at + sizeof(uint32_t) + kCoordinates * sizeof(float);
    pose.look.yaw_deg = z13::gameplay::DequantizeActionValue(static_cast<int16_t>(Get<uint16_t>(bytes, angles)));
    pose.look.pitch_deg = z13::gameplay::DequantizeActionValue(
        static_cast<int16_t>(Get<uint16_t>(bytes, angles + sizeof(int16_t))));
    packet.poses.push_back(pose);
  }
  return packet;
}

}  // namespace z13::net
