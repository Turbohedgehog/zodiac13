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

#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace z13::net {

namespace {

constexpr std::string_view kClosedSessionError = "operation on a closed session";

std::unexpected<std::string> ClosedSession() {
  return std::unexpected(std::string(kClosedSessionError));
}

}  // namespace

class NetSession::State {
 public:
  bool IsOpen() const { return transport != nullptr; }

  std::unique_ptr<Transport> transport;
  std::unordered_set<ConnectionId> connections;
  std::unordered_map<ConnectionId, uint32_t> connection_to_player;
  std::optional<ConnectionId> server_connection;
};

void NetSession::OpenAsServer(std::unique_ptr<Transport> transport) {
  state_ = std::make_shared<State>();
  state_->transport = std::move(transport);
}

void NetSession::OpenAsClient(std::unique_ptr<Transport> transport) {
  state_ = std::make_shared<State>();
  state_->transport = std::move(transport);
}

bool NetSession::IsOpen() const {
  return state_ && state_->IsOpen();
}

void NetSession::Close() {
  state_.reset();
}

std::expected<std::vector<TransportEvent>, std::string> NetSession::Service() {
  if (!IsOpen()) {
    return ClosedSession();
  }
  return state_->transport->Service();
}

NetSession::Result NetSession::Send(ConnectionId connection, Channel channel, const Envelope& envelope) {
  if (!IsOpen()) {
    return ClosedSession();
  }
  const std::vector<uint8_t> bytes = EncodeMessage(envelope);
  state_->transport->Send(connection, channel, std::as_bytes(std::span(bytes)));
  return {};
}

NetSession::Result NetSession::Broadcast(
    Channel channel, const Envelope& envelope, std::optional<ConnectionId> except) {
  if (!IsOpen()) {
    return ClosedSession();
  }
  const std::vector<uint8_t> bytes = EncodeMessage(envelope);
  const auto payload = std::as_bytes(std::span(bytes));
  for (const ConnectionId connection : state_->connections) {
    if (except && connection == *except) {
      continue;
    }
    state_->transport->Send(connection, channel, payload);
  }
  return {};
}

NetSession::Result NetSession::Disconnect(ConnectionId connection) {
  if (!IsOpen()) {
    return ClosedSession();
  }
  state_->transport->Disconnect(connection);
  return {};
}

NetSession::Result NetSession::AddConnection(ConnectionId connection) {
  if (!IsOpen()) {
    return ClosedSession();
  }
  state_->connections.insert(connection);
  return {};
}

NetSession::Result NetSession::RemoveConnection(ConnectionId connection) {
  if (!IsOpen()) {
    return ClosedSession();
  }
  state_->connections.erase(connection);
  state_->connection_to_player.erase(connection);
  if (state_->server_connection == connection) {
    state_->server_connection.reset();
  }
  return {};
}

NetSession::Result NetSession::BindPlayer(ConnectionId connection, uint32_t player_id) {
  if (!IsOpen()) {
    return ClosedSession();
  }
  state_->connection_to_player[connection] = player_id;
  return {};
}

std::optional<uint32_t> NetSession::PlayerIdFor(ConnectionId connection) const {
  if (!IsOpen()) {
    return std::nullopt;
  }
  const auto player = state_->connection_to_player.find(connection);
  return player != state_->connection_to_player.end() ? std::optional(player->second) : std::nullopt;
}

NetSession::Result NetSession::SetServerConnection(ConnectionId connection) {
  if (!IsOpen()) {
    return ClosedSession();
  }
  state_->server_connection = connection;
  return {};
}

std::optional<ConnectionId> NetSession::ServerConnection() const {
  return IsOpen() ? state_->server_connection : std::nullopt;
}

}  // namespace z13::net
