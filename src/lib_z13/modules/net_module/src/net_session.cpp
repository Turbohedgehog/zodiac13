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

#include "net_session.h"

#include <cassert>
#include <span>
#include <unordered_map>
#include <unordered_set>

namespace z13::net {

// No transport means a closed session.
class NetSession::State {
 public:
  std::unique_ptr<Transport> transport;
  std::unordered_set<ConnectionId> connections;
  std::unordered_map<ConnectionId, uint32_t> connection_to_player;
  std::optional<ConnectionId> server_connection;
};

void NetSession::Open(std::unique_ptr<Transport> transport) {
  state_ = std::make_shared<State>();
  state_->transport = std::move(transport);
}

bool NetSession::IsOpen() const {
  return state_ && state_->transport;
}

void NetSession::Close() {
  state_.reset();
}

NetSession::State& NetSession::OpenState() const {
  assert(IsOpen() && "NetSession used after Close()");
  return *state_;
}

std::vector<TransportEvent> NetSession::Service() {
  return OpenState().transport->Service();
}

void NetSession::Send(ConnectionId connection, Channel channel, const Envelope& envelope) {
  const std::vector<uint8_t> bytes = EncodeMessage(envelope);
  OpenState().transport->Send(connection, channel, std::as_bytes(std::span(bytes)));
}

void NetSession::Broadcast(Channel channel, const Envelope& envelope, std::optional<ConnectionId> except) {
  State& state = OpenState();
  const std::vector<uint8_t> bytes = EncodeMessage(envelope);
  const auto payload = std::as_bytes(std::span(bytes));
  for (const ConnectionId connection : state.connections) {
    if (connection != except) {
      state.transport->Send(connection, channel, payload);
    }
  }
}

void NetSession::Disconnect(ConnectionId connection) {
  OpenState().transport->Disconnect(connection);
}

void NetSession::AddConnection(ConnectionId connection) {
  OpenState().connections.insert(connection);
}

void NetSession::RemoveConnection(ConnectionId connection) {
  State& state = OpenState();
  state.connections.erase(connection);
  state.connection_to_player.erase(connection);
  if (state.server_connection == connection) {
    state.server_connection.reset();
  }
}

void NetSession::BindPlayer(ConnectionId connection, uint32_t player_id) {
  OpenState().connection_to_player[connection] = player_id;
}

std::optional<uint32_t> NetSession::PlayerIdFor(ConnectionId connection) const {
  const auto& by_connection = OpenState().connection_to_player;
  const auto player = by_connection.find(connection);
  return player != by_connection.end() ? std::optional(player->second) : std::nullopt;
}

void NetSession::SetServerConnection(ConnectionId connection) {
  OpenState().server_connection = connection;
}

std::optional<ConnectionId> NetSession::ServerConnection() const {
  return OpenState().server_connection;
}

}  // namespace z13::net
