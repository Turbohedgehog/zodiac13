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
#include <memory>
#include <optional>
#include <vector>

#include <net_module/protocol.h>
#include <net_module/transport.h>

namespace z13::net {

// The live Transport plus connection/player bookkeeping, as a singleton exempt from the
// "no pointers in components" rule like PhysicsWorld (see CLAUDE.md).
class NetSession {
 public:
  using Singleton = void;

  void Open(std::unique_ptr<Transport> transport);
  void Close();

  // The calls below need an open session: one is in the world only while open (EndSession).

  std::vector<TransportEvent> Service();

  void Send(ConnectionId connection, Channel channel, const Envelope& envelope);
  // Every tracked connection (AddConnection) except `except`, if given.
  void Broadcast(Channel channel, const Envelope& envelope, std::optional<ConnectionId> except = {});
  // Bookkeeping is cleaned up on the kDisconnected event this produces.
  void Disconnect(ConnectionId connection);

  // Server: every accepted connection; client: the one to the server.
  void AddConnection(ConnectionId connection);
  void RemoveConnection(ConnectionId connection);

  // Server-side, once a ClientHello is accepted.
  void BindPlayer(ConnectionId connection, uint32_t player_id);
  std::optional<uint32_t> PlayerIdFor(ConnectionId connection) const;

  // Client-side, from the first kConnected event.
  void SetServerConnection(ConnectionId connection);
  std::optional<ConnectionId> ServerConnection() const;

 private:
  class State;

  std::shared_ptr<State> state_;
};

}  // namespace z13::net
