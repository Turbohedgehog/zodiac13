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

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <type_traits>
#include <vector>

#include <net_generated.h>

namespace z13::net {

constexpr uint32_t kProtocolVersion = 2;

// MessageEnvelopeT's `body` is already a tagged union over every wire message
// (MessageBodyUnion::type/Set<T>()/AsXxx(), net_generated.h) -- no variant needed.
using Envelope = fbs::net::MessageEnvelopeT;

std::vector<uint8_t> EncodeMessage(const Envelope& envelope);

// Runs a flatbuffers::Verifier over `bytes` first -- untrusted input (network, not a
// local save file) must never reach the generated accessors unchecked.
std::expected<Envelope, std::string> DecodeMessage(std::span<const uint8_t> bytes);

// A decoded envelope's body as T, or null if it holds a different message type. Mirrors
// MessageBodyUnion::AsXxx() generically instead of a hand-written switch per T.
template <typename T>
const T* AsBody(const fbs::net::MessageBodyUnion& body) {
  return body.type == fbs::net::MessageBodyUnionTraits<T>::enum_value ? static_cast<const T*>(body.value) : nullptr;
}

template <typename... Ts>
struct Overloaded : Ts... {
  using Ts::operator()...;
};

namespace internal {

template <typename Result, typename Visitor, typename T, typename... Rest>
Result VisitBodyAs(const fbs::net::MessageBodyUnion& body, Visitor& visitor) {
  if (const T* message = AsBody<T>(body)) {
    return visitor(*message);
  }
  if constexpr (sizeof...(Rest) == 0) {
    return Result();
  } else {
    return VisitBodyAs<Result, Visitor, Rest...>(body, visitor);
  }
}

template <typename... Messages, typename Visitor>
auto VisitBodyOf(const fbs::net::MessageBodyUnion& body, Visitor& visitor) {
  static_assert(sizeof...(Messages) == static_cast<size_t>(fbs::net::MessageBody::MAX), "list every MessageBody type");
  using Result = std::common_type_t<std::invoke_result_t<Visitor&, const Messages&>...>;
  return VisitBodyAs<Result, Visitor, Messages...>(body, visitor);
}

}  // namespace internal

// std::visit for an envelope body: calls `visitor` with the concrete message. An empty
// body yields a default-constructed result.
template <typename Visitor>
auto VisitBody(const fbs::net::MessageBodyUnion& body, Visitor&& visitor) {
  namespace fbn = fbs::net;
  return internal::VisitBodyOf<
      fbn::ClientHelloT, fbn::CommandBatchT, fbn::ResyncRequestT, fbn::PingT, fbn::PongT, fbn::WelcomeT,
      fbn::RejectedT, fbn::ResyncT, fbn::PlayerJoinedT, fbn::PlayerLeftT, fbn::SequencedCommandsT,
      fbn::StateDigestT>(body, visitor);
}

// CommandWire::value is a quantized int16; docs/client-server-plan.md requires the
// sender to apply the same quantized value everyone else receives, not the original
// float, so these two are the only place that quantization happens.
constexpr float kActionValueScale = 100.f;
int16_t QuantizeActionValue(float value);
float DequantizeActionValue(int16_t wire_value);

}  // namespace z13::net
