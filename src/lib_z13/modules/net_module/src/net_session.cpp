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
#include <unordered_map>
#include <unordered_set>

namespace z13::net {

class NetSession::State {
 public:
  std::unique_ptr<Transport> transport;
  std::unordered_set<ConnectionId> connections;
  std::unordered_map<ConnectionId, uint32_t> connection_to_player;
  std::unordered_map<uint32_t, ConnectionId> player_to_connection;
  std::optional<ConnectionId> server_connection;
  SentTraffic sent;
  std::unordered_map<ConnectionId, fbs::net::SequencedCommandsT> pending_runs;

  // Anything else sent on the reliable channel goes after the runs queued before it.
  void FlushRuns(ConnectionId connection) {
    const auto pending = pending_runs.find(connection);
    if (pending == pending_runs.end()) {
      return;
    }
    Envelope envelope;
    envelope.body.Set(std::move(pending->second));
    pending_runs.erase(pending);
    const std::vector<uint8_t> bytes = EncodeMessage(envelope);
    sent.Record({connection, Channel::kReliable}, envelope.body.type, bytes.size());
    transport->Send(connection, Channel::kReliable, std::as_bytes(std::span(bytes)));
  }
};

void NetSession::Open(std::unique_ptr<Transport> transport) {
  state_ = std::make_shared<State>();
  state_->transport = std::move(transport);
}

void NetSession::Close() {
  state_.reset();
}

std::vector<TransportEvent> NetSession::Service() {
  return state_->transport->Service();
}

std::vector<Datagram> NetSession::TakeDatagrams() {
  return state_->transport->TakeDatagrams();
}

Status NetSession::SendDatagram(const Endpoint& to, std::span<const uint8_t> bytes) {
  return state_->transport->SendDatagram(to, bytes);
}

void NetSession::Send(ConnectionId connection, Channel channel, const Envelope& envelope) {
  if (channel == Channel::kReliable) {
    state_->FlushRuns(connection);
  }
  const std::vector<uint8_t> bytes = EncodeMessage(envelope);
  state_->sent.Record({connection, channel}, envelope.body.type, bytes.size());
  state_->transport->Send(connection, channel, std::as_bytes(std::span(bytes)));
}

void NetSession::SendBytes(ConnectionId connection, Channel channel, std::span<const uint8_t> bytes) {
  state_->sent.Record({connection, channel}, std::nullopt, bytes.size());
  state_->transport->Send(connection, channel, std::as_bytes(bytes));
}

void NetSession::Broadcast(Channel channel, const Envelope& envelope, std::optional<ConnectionId> except) {
  State& state = *state_;
  const std::vector<uint8_t> bytes = EncodeMessage(envelope);
  const auto payload = std::as_bytes(std::span(bytes));
  for (const ConnectionId connection : state.connections) {
    if (connection != except) {
      if (channel == Channel::kReliable) {
        state.FlushRuns(connection);
      }
      state.sent.Record({connection, channel}, envelope.body.type, bytes.size());
      state.transport->Send(connection, channel, payload);
    }
  }
}

void NetSession::Multicast(Channel channel, const Envelope& envelope, std::span<const ConnectionId> connections) {
  State& state = *state_;
  const std::vector<uint8_t> bytes = EncodeMessage(envelope);
  const auto payload = std::as_bytes(std::span(bytes));
  for (const ConnectionId connection : connections) {
    if (channel == Channel::kReliable) {
      state.FlushRuns(connection);
    }
    state.sent.Record({connection, channel}, envelope.body.type, bytes.size());
    state.transport->Send(connection, channel, payload);
  }
}

void NetSession::QueueRun(
    std::span<const ConnectionId> connections, const fbs::net::CommandRunWire& run,
    std::span<const fbs::net::CommandWire> commands) {
  for (const ConnectionId connection : connections) {
    fbs::net::SequencedCommandsT& pending = state_->pending_runs[connection];
    pending.runs.push_back(run);
    pending.commands.insert(pending.commands.end(), commands.begin(), commands.end());
  }
}

void NetSession::FlushRelays() {
  State& state = *state_;
  std::vector<ConnectionId> queued;
  for (const auto& [connection, runs] : state.pending_runs) {
    queued.push_back(connection);
  }
  for (const ConnectionId connection : queued) {
    state.FlushRuns(connection);
  }
}

std::vector<ConnectionId> NetSession::Connections() const {
  return {state_->connections.begin(), state_->connections.end()};
}

const SentTraffic& NetSession::Sent() const {
  return state_->sent;
}

void NetSession::Disconnect(ConnectionId connection) {
  state_->transport->Disconnect(connection);
}

void NetSession::AddConnection(ConnectionId connection) {
  state_->connections.insert(connection);
}

void NetSession::RemoveConnection(ConnectionId connection) {
  State& state = *state_;
  state.connections.erase(connection);
  state.pending_runs.erase(connection);
  if (const auto player = state.connection_to_player.find(connection); player != state.connection_to_player.end()) {
    state.player_to_connection.erase(player->second);
    state.connection_to_player.erase(player);
  }
  if (state.server_connection == connection) {
    state.server_connection.reset();
  }
}

void NetSession::BindPlayer(ConnectionId connection, uint32_t player_id) {
  state_->connection_to_player[connection] = player_id;
  state_->player_to_connection[player_id] = connection;
}

std::optional<uint32_t> NetSession::PlayerIdFor(ConnectionId connection) const {
  const auto& by_connection = state_->connection_to_player;
  const auto player = by_connection.find(connection);
  return player != by_connection.end() ? std::optional(player->second) : std::nullopt;
}

std::optional<ConnectionId> NetSession::ConnectionOf(uint32_t player_id) const {
  const auto& by_player = state_->player_to_connection;
  const auto connection = by_player.find(player_id);
  return connection != by_player.end() ? std::optional(connection->second) : std::nullopt;
}

size_t NetSession::PlayerCount() const {
  return state_->connection_to_player.size();
}

std::vector<std::pair<ConnectionId, uint32_t>> NetSession::PlayerConnections() const {
  const auto& by_connection = state_->connection_to_player;
  return {by_connection.begin(), by_connection.end()};
}

void NetSession::SetServerConnection(ConnectionId connection) {
  state_->server_connection = connection;
}

std::optional<ConnectionId> NetSession::ServerConnection() const {
  return state_->server_connection;
}

}  // namespace z13::net
