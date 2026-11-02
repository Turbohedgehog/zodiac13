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

#include <net_module/enet_transport.h>

#include <format>
#include <ranges>
#include <unordered_map>
#include <utility>

#include <enet/enet.h>

#include <net_module/discovery_messages.h>

#include "enet_datagram.h"
#include "enet_lifetime.h"

namespace z13::net {

namespace {

constexpr size_t kChannelCount = 4;  // Channel::kReliable, kUnreliable, kHints, kPoses

struct EnetHostDeleter {
  void operator()(ENetHost* host) const {
    if (host) {
      enet_host_destroy(host);
    }
  }
};
using EnetHostPtr = std::unique_ptr<ENetHost, EnetHostDeleter>;

// Sees every datagram reaching a server's port before ENet does: a discovery query comes out of
// enet_host_service as a peerless receive, read in Service() from the host, which keeps the
// datagram until its next receive.
int ENET_CALLBACK InterceptDiscovery(ENetHost* host, ENetEvent* event) {
  if (event == nullptr || !IsDiscoveryMessage({host->receivedData, host->receivedDataLength})) {
    return 0;
  }
  event->type = ENET_EVENT_TYPE_RECEIVE;
  event->peer = nullptr;
  event->packet = nullptr;
  return 1;
}

class EnetTransport final : public Transport {
 public:
  EnetTransport(std::shared_ptr<EnetLifetime> lifetime, EnetHostPtr host, z13::SessionTimeoutConfig session_timeout)
      : lifetime_(std::move(lifetime)), host_(std::move(host)), session_timeout_(std::move(session_timeout)) {}

  // Sent now: destroying the host would drop a queued enet_peer_disconnect unsent.
  ~EnetTransport() override {
    for (ENetPeer* peer : peers_by_id_ | std::views::values) {
      enet_peer_disconnect_now(peer, 0);
    }
  }

  void Send(ConnectionId connection, Channel channel, std::span<const std::byte> data) override {
    ENetPeer* peer = FindPeer(connection);
    if (!peer) {
      return;
    }
    const enet_uint32 flags = channel == Channel::kReliable ? ENET_PACKET_FLAG_RELIABLE : 0;
    ENetPacket* packet = enet_packet_create(data.data(), data.size(), flags);
    enet_peer_send(peer, static_cast<enet_uint8>(channel), packet);
  }

  void Disconnect(ConnectionId connection) override {
    if (ENetPeer* peer = FindPeer(connection)) {
      enet_peer_disconnect(peer, 0);
    }
  }

  std::vector<TransportEvent> Service() override {
    std::vector<TransportEvent> events;
    ENetEvent event;
    while (enet_host_service(host_.get(), &event, 0) > 0) {
      switch (event.type) {
        case ENET_EVENT_TYPE_CONNECT: {
          enet_peer_timeout(event.peer, session_timeout_.limit, session_timeout_.min_timeout_ms,
                            session_timeout_.max_timeout_ms);
          events.push_back({.kind = TransportEventKind::kConnected, .connection = AssignConnectionId(event.peer)});
          break;
        }
        case ENET_EVENT_TYPE_DISCONNECT: {
          events.push_back({.kind = TransportEventKind::kDisconnected, .connection = ReleaseConnectionId(event.peer)});
          break;
        }
        case ENET_EVENT_TYPE_RECEIVE: {
          if (event.peer == nullptr) {
            datagrams_.push_back({
                .from = EndpointOf(host_->receivedAddress),
                .bytes = {host_->receivedData, host_->receivedData + host_->receivedDataLength},
            });
            break;
          }
          const auto* begin = reinterpret_cast<const std::byte*>(event.packet->data);
          events.push_back({
              .kind = TransportEventKind::kReceived,
              .connection = FindConnectionId(event.peer),
              .channel = static_cast<Channel>(event.channelID),
              .data = std::vector<std::byte>(begin, begin + event.packet->dataLength),
          });
          enet_packet_destroy(event.packet);
          break;
        }
        case ENET_EVENT_TYPE_NONE:
          break;
      }
    }
    return events;
  }

  std::vector<Datagram> TakeDatagrams() override {
    return std::exchange(datagrams_, {});
  }

  Status SendDatagram(const Endpoint& to, std::span<const uint8_t> bytes) override {
    return z13::net::SendDatagram(host_->socket, to, bytes);
  }

 private:
  ConnectionId AssignConnectionId(ENetPeer* peer) {
    const ConnectionId id = next_connection_id_++;
    peer->data = reinterpret_cast<void*>(static_cast<uintptr_t>(id));
    peers_by_id_[id] = peer;
    return id;
  }

  ConnectionId ReleaseConnectionId(ENetPeer* peer) {
    const ConnectionId id = FindConnectionId(peer);
    peers_by_id_.erase(id);
    peer->data = nullptr;
    return id;
  }

  static ConnectionId FindConnectionId(const ENetPeer* peer) {
    return peer ? static_cast<ConnectionId>(reinterpret_cast<uintptr_t>(peer->data)) : kInvalidConnectionId;
  }

  ENetPeer* FindPeer(ConnectionId connection) const {
    const auto it = peers_by_id_.find(connection);
    return it != peers_by_id_.end() ? it->second : nullptr;
  }

  std::shared_ptr<EnetLifetime> lifetime_;
  EnetHostPtr host_;
  std::unordered_map<ConnectionId, ENetPeer*> peers_by_id_;
  std::vector<Datagram> datagrams_;
  z13::SessionTimeoutConfig session_timeout_;
  ConnectionId next_connection_id_ = 1;
};

}  // namespace

std::expected<std::unique_ptr<Transport>, std::string> CreateEnetServerTransport(
    uint16_t port, size_t max_clients, z13::SessionTimeoutConfig session_timeout) {
  auto lifetime = AcquireEnetLifetime();
  if (!lifetime) {
    return std::unexpected(lifetime.error());
  }

  ENetAddress address {};
  address.host = ENET_HOST_ANY;
  address.port = port;
  EnetHostPtr host(enet_host_create(&address, max_clients, kChannelCount, 0, 0));
  if (!host) {
    return std::unexpected(std::format("enet_host_create failed to bind port {}", port));
  }
  host->intercept = InterceptDiscovery;
  return std::make_unique<EnetTransport>(std::move(*lifetime), std::move(host), std::move(session_timeout));
}

std::expected<std::unique_ptr<Transport>, std::string> CreateEnetClientTransport(
    const Endpoint& server, z13::ConnectTimeoutConfig connect_timeout, z13::SessionTimeoutConfig session_timeout) {
  auto lifetime = AcquireEnetLifetime();
  if (!lifetime) {
    return std::unexpected(lifetime.error());
  }

  EnetHostPtr host(enet_host_create(nullptr, 1, kChannelCount, 0, 0));
  if (!host) {
    return std::unexpected("enet_host_create failed to create a client host");
  }

  ENetAddress address {};
  if (enet_address_set_host(&address, server.host.c_str()) != 0) {
    return std::unexpected(std::format("could not resolve host '{}'", server.host));
  }
  address.port = server.port;
  ENetPeer* peer = enet_host_connect(host.get(), &address, kChannelCount, 0);
  if (!peer) {
    return std::unexpected("enet_host_connect failed (no available peer slots)");
  }
  enet_peer_timeout(
      peer, connect_timeout.limit, connect_timeout.min_timeout_ms, connect_timeout.max_timeout_ms);
  return std::make_unique<EnetTransport>(std::move(*lifetime), std::move(host), std::move(session_timeout));
}

}  // namespace z13::net
