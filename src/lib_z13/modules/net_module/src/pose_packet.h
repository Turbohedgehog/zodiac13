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

#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

#include <Eigen/Dense>

#include <lib_core/utils/camera_pose.h>

namespace z13::net {

// Where a player stands and looks, for a client that draws it without simulating it (a ghost).
struct PoseRecord {
  uint32_t player_id {};
  Eigen::Vector3f position = Eigen::Vector3f::Zero();
  z13::LookAngles look;
};

// The poses of one server tick. They travel as packets of their own on Channel::kPoses, unreliable: a lost
// one is replaced by the next.
struct PosePacket {
  uint64_t tick {};
  std::vector<PoseRecord> poses;
};

// What one packet carries, so that it fits in an ENet datagram (about 1390 bytes); a longer list is cut.
constexpr size_t kMaxPosesPerPacket = 60;

// The tick's low 32 bits, the count, and per pose the player (u32), the position (three f32) and the angles in
// 0.01 degrees (two i16, as an action's value), little-endian.
std::vector<uint8_t> EncodePoses(const PosePacket& packet);

// `now` is the receiver's tick; the packet's tick is the one near it with those low 32 bits.
// Untrusted input: a short, over-long or inconsistent packet is an error.
std::expected<PosePacket, std::string> DecodePoses(std::span<const uint8_t> bytes, uint64_t now);

}  // namespace z13::net
