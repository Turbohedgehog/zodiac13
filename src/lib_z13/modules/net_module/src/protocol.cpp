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

#include <net_module/protocol.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace z13::net {

namespace {

namespace fbn = fbs::net;

}  // namespace

std::vector<uint8_t> EncodeMessage(const Envelope& envelope) {
  flatbuffers::FlatBufferBuilder builder;
  builder.Finish(fbn::MessageEnvelope::Pack(builder, &envelope));
  return {builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize()};
}

std::expected<Envelope, std::string> DecodeMessage(std::span<const uint8_t> bytes) {
  flatbuffers::Verifier verifier(bytes.data(), bytes.size());
  if (!fbn::VerifyMessageEnvelopeBuffer(verifier)) {
    return std::unexpected("malformed MessageEnvelope buffer");
  }

  const fbn::MessageEnvelope* root = fbn::GetMessageEnvelope(bytes.data());
  if (root->body_type() == fbn::MessageBody::NONE) {
    return std::unexpected("MessageEnvelope has no body");
  }

  Envelope envelope;
  root->UnPackTo(&envelope);
  return envelope;
}

int16_t QuantizeActionValue(float value) {
  const float scaled = std::clamp(
      value * kActionValueScale, static_cast<float>(std::numeric_limits<int16_t>::min()),
      static_cast<float>(std::numeric_limits<int16_t>::max()));
  return static_cast<int16_t>(std::lround(scaled));
}

float DequantizeActionValue(int16_t wire_value) {
  return static_cast<float>(wire_value) / kActionValueScale;
}

}  // namespace z13::net
