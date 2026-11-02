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

#include "hint_packet.h"

#include <algorithm>
#include <format>
#include <ranges>

#include "byte_codec.h"

namespace z13::net {

namespace {

constexpr size_t kPlayerBytes = sizeof(uint32_t);
constexpr size_t kTickBytes = sizeof(uint32_t);
constexpr size_t kCountBytes = sizeof(uint8_t);
constexpr size_t kRecordBytes = 2 * sizeof(uint16_t);

std::vector<uint8_t> Encode(const HintPacket& packet, bool with_player) {
  const size_t count = std::min(packet.hints.size(), kMaxHintsPerPacket);
  std::vector<uint8_t> out;
  out.reserve(kPlayerBytes + kTickBytes + kCountBytes + count * kRecordBytes);
  if (with_player) {
    Put(out, packet.player_id);
  }
  Put(out, static_cast<uint32_t>(packet.tick));
  out.push_back(static_cast<uint8_t>(count));
  for (const HintRecord& hint : packet.hints | std::views::take(count)) {
    Put(out, hint.action_id);
    Put(out, static_cast<uint16_t>(hint.value));
  }
  return out;
}

std::expected<HintPacket, std::string> Decode(std::span<const uint8_t> bytes, uint64_t now, bool with_player) {
  const size_t header = (with_player ? kPlayerBytes : 0) + kTickBytes + kCountBytes;
  if (bytes.size() < header) {
    return std::unexpected(std::format("a hint packet of {} bytes is shorter than its header", bytes.size()));
  }
  size_t at = 0;
  HintPacket packet;
  if (with_player) {
    packet.player_id = Get<uint32_t>(bytes, at);
    at += kPlayerBytes;
  }
  packet.tick = ResolveTick(now, Get<uint32_t>(bytes, at));
  at += kTickBytes;
  const size_t count = bytes[at];
  at += kCountBytes;
  if (count > kMaxHintsPerPacket || bytes.size() != header + count * kRecordBytes) {
    return std::unexpected(std::format("a hint packet of {} bytes does not hold {} hints", bytes.size(), count));
  }
  for (size_t i = 0; i < count; ++i, at += kRecordBytes) {
    packet.hints.push_back(
        {.action_id = Get<uint16_t>(bytes, at),
         .value = static_cast<int16_t>(Get<uint16_t>(bytes, at + sizeof(uint16_t)))});
  }
  return packet;
}

}  // namespace

std::vector<uint8_t> EncodeOwnHints(const HintPacket& packet) {
  return Encode(packet, false);
}

std::vector<uint8_t> EncodeRelayedHints(const HintPacket& packet) {
  return Encode(packet, true);
}

std::expected<HintPacket, std::string> DecodeOwnHints(std::span<const uint8_t> bytes, uint64_t now) {
  return Decode(bytes, now, false);
}

std::expected<HintPacket, std::string> DecodeRelayedHints(std::span<const uint8_t> bytes, uint64_t now) {
  return Decode(bytes, now, true);
}

}  // namespace z13::net
