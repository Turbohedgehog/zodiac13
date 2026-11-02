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

#include <net_module/discovery_messages.h>

#include <utility>

#include <flatbuffers/flatbuffers.h>

namespace z13::net {

std::vector<uint8_t> EncodeDiscovery(const DiscoveryMessage& message) {
  flatbuffers::FlatBufferBuilder builder;
  builder.Finish(fbs::discovery::DiscoveryMessage::Pack(builder, &message), fbs::discovery::DiscoveryMessageIdentifier());
  return {builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize()};
}

bool IsDiscoveryMessage(std::span<const uint8_t> bytes) {
  flatbuffers::Verifier verifier(bytes.data(), bytes.size());
  return bytes.size() >= flatbuffers::kFileIdentifierLength + sizeof(flatbuffers::uoffset_t) &&
         fbs::discovery::DiscoveryMessageBufferHasIdentifier(bytes.data()) &&
         fbs::discovery::VerifyDiscoveryMessageBuffer(verifier);
}

std::expected<DiscoveryMessage, std::string> DecodeDiscovery(std::span<const uint8_t> bytes) {
  if (!IsDiscoveryMessage(bytes)) {
    return std::unexpected(std::string("not a discovery message"));
  }
  DiscoveryMessage message;
  fbs::discovery::GetDiscoveryMessage(bytes.data())->UnPackTo(&message);
  return message;
}

std::vector<uint8_t> EncodeQuery(uint64_t sent_at_ms) {
  DiscoveryMessage message;
  message.body.Set(fbs::discovery::DiscoveryQueryT {.format_version = kDiscoveryFormatVersion, .sent_at_ms = sent_at_ms});
  return EncodeDiscovery(message);
}

std::optional<std::vector<uint8_t>> ReplyTo(std::span<const uint8_t> query, ServerInfo info) {
  const auto message = DecodeDiscovery(query);
  if (!message) {
    return std::nullopt;
  }
  const auto* asked = message->body.AsDiscoveryQuery();
  if (asked == nullptr || asked->format_version != kDiscoveryFormatVersion) {
    return std::nullopt;
  }
  info.sent_at_ms = asked->sent_at_ms;
  DiscoveryMessage reply;
  reply.body.Set(std::move(info));
  return EncodeDiscovery(reply);
}

}  // namespace z13::net
