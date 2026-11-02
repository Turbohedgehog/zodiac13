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

namespace z13::net {

// An action's value as a hint: the same 0.01 steps as a command's (QuantizeActionValue).
struct HintRecord {
  uint16_t action_id {};
  int16_t value {};

  bool operator==(const HintRecord&) const = default;
};

// The hints a player sent for one tick. They travel as packets of their own on Channel::kHints,
// not as FlatBuffers messages: a table and its envelope cost ~50 bytes around four bytes of value.
struct HintPacket {
  uint32_t player_id {};
  uint64_t tick {};
  std::vector<HintRecord> hints;

  bool operator==(const HintPacket&) const = default;
};

// What one packet may carry; a longer one is cut, not sent.
constexpr size_t kMaxHintsPerPacket = 16;

// A client's own hints to the server, which knows the player from the connection: the tick's low
// 32 bits, the count, and (action u16, value i16) per hint, little-endian.
std::vector<uint8_t> EncodeOwnHints(const HintPacket& packet);

// The server's relay: the player id (u32) ahead of the same bytes.
std::vector<uint8_t> EncodeRelayedHints(const HintPacket& packet);

// `now` is the receiver's tick; the packet's tick is the one near it with those low 32 bits.
// Untrusted input: a short, over-long or inconsistent packet is an error.
std::expected<HintPacket, std::string> DecodeOwnHints(std::span<const uint8_t> bytes, uint64_t now);
std::expected<HintPacket, std::string> DecodeRelayedHints(std::span<const uint8_t> bytes, uint64_t now);

}  // namespace z13::net
