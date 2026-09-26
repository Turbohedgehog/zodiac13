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
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <net_module/protocol.h>
#include <net_module/transport.h>

namespace z13::net {

// The live Transport plus connection/player bookkeeping, as a singleton exempt from the
// "no pointers in components" rule like PhysicsWorld (see CLAUDE.md). Message-handling
// logic lives in net_session_system.cpp, not here.
class NetSession {
 public:
  using Singleton = void;
  // Every call below needs an open session. On a closed one they report instead of
  // quietly doing nothing, so a sequencing bug surfaces at ServiceNetSession.
  using Result = std::expected<void, std::string>;

  void OpenAsServer(std::unique_ptr<Transport> transport);
  void OpenAsClient(std::unique_ptr<Transport> transport);
  bool IsOpen() const;
  void Close();

  // Pumps the transport.
  std::expected<std::vector<TransportEvent>, std::string> Service();

  Result Send(ConnectionId connection, Channel channel, const Envelope& envelope);
  // Every tracked connection (AddConnection) except `except`, if given.
  Result Broadcast(Channel channel, const Envelope& envelope, std::optional<ConnectionId> except = {});
  // Drops a misbehaving peer at the transport level; bookkeeping is cleaned up
  // separately, from the kDisconnected event this produces.
  Result Disconnect(ConnectionId connection);

  // All connections accepted so far (server) or the one outgoing connection (client),
  // independent of whether a player id is bound yet.
  Result AddConnection(ConnectionId connection);
  Result RemoveConnection(ConnectionId connection);

  // Server-side connection <-> player id, set once a ClientHello is accepted.
  Result BindPlayer(ConnectionId connection, uint32_t player_id);
  // Nullopt for an unbound connection as well as a closed session -- both mean the same
  // thing to every caller, so neither is an error worth reporting.
  std::optional<uint32_t> PlayerIdFor(ConnectionId connection) const;

  // Client-side: the one connection representing the server, from the first
  // kConnected event this session's transport produces.
  Result SetServerConnection(ConnectionId connection);
  std::optional<ConnectionId> ServerConnection() const;

 private:
  class State;

  std::shared_ptr<State> state_;
};

using SessionDelta = std::variant<fbs::net::PlayerJoinedT, fbs::net::PlayerLeftT>;

struct ScheduledSessionDelta {
  uint64_t apply_tick {};
  SessionDelta delta;
};

// Session events scheduled by apply_tick, same as commands: a client's own join can
// need to land exactly on the last tick its catch-up replays, and an already-connected
// client's view of someone else's join/leave must apply at the same tick everywhere.
struct ScheduledSessionDeltas {
  using Singleton = void;
  std::vector<ScheduledSessionDelta> pending;
};

}  // namespace z13::net
