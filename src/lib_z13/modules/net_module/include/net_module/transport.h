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
#include <span>
#include <vector>

namespace z13::net {

using ConnectionId = uint32_t;
constexpr ConnectionId kInvalidConnectionId = 0;

// Reliable: CommandBatch/SequencedCommands/session messages -- a lost one would
// break determinism. Unreliable: Ping/Pong -- see docs/client-server-plan.md's
// protocol table for why they're split this way.
enum class Channel : uint8_t { kReliable = 0, kUnreliable = 1 };

enum class TransportEventKind { kConnected, kDisconnected, kReceived };

struct TransportEvent {
  TransportEventKind kind {};
  ConnectionId connection = kInvalidConnectionId;
  Channel channel = Channel::kReliable;  // kReceived only
  std::vector<std::byte> data;           // kReceived only
};

// One ENet host: either listening for connections (server) or a single outgoing
// connection (client) -- see enet_transport.h/in_memory_transport.h's factory
// functions, which decide that at construction. Serviced once per frame from a
// system (Service()); no threads, matching the project's determinism requirement.
class Transport {
 public:
  virtual ~Transport() = default;

  virtual void Send(ConnectionId connection, Channel channel, std::span<const std::byte> data) = 0;
  virtual void Disconnect(ConnectionId connection) = 0;

  // Pumps the transport and returns every event queued since the last call.
  virtual std::vector<TransportEvent> Service() = 0;
};

}  // namespace z13::net
