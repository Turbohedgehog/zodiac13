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
#include <unordered_map>

#include <enet/enet.h>

namespace z13::net {

namespace {

constexpr size_t kChannelCount = 2;  // Channel::kReliable, Channel::kUnreliable

// enet_initialize()/enet_deinitialize() are process-global, not per-host, but
// Transport instances are created and destroyed independently (one per test, per
// connection...); this keeps them paired without a bare static counter. The weak_ptr
// itself is the one justified exception to "no static/globals" (CLAUDE.md) -- ENet's
// own init call has no other way to know "am I the last host" (mirrors
// g_interruptible_core in z13_launcher.cpp).
class EnetLifetime {
 public:
  ~EnetLifetime() { enet_deinitialize(); }
};

std::expected<std::shared_ptr<EnetLifetime>, std::string> AcquireEnetLifetime() {
  static std::weak_ptr<EnetLifetime> weak_instance;
  if (auto existing = weak_instance.lock()) {
    return existing;
  }
  if (enet_initialize() != 0) {
    return std::unexpected("enet_initialize failed");
  }
  auto instance = std::shared_ptr<EnetLifetime>(new EnetLifetime());
  weak_instance = instance;
  return instance;
}

struct EnetHostDeleter {
  void operator()(ENetHost* host) const {
    if (host) {
      enet_host_destroy(host);
    }
  }
};
using EnetHostPtr = std::unique_ptr<ENetHost, EnetHostDeleter>;

class EnetTransport final : public Transport {
 public:
  EnetTransport(std::shared_ptr<EnetLifetime> lifetime, EnetHostPtr host)
      : lifetime_(std::move(lifetime)), host_(std::move(host)) {}

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
          events.push_back({.kind = TransportEventKind::kConnected, .connection = AssignConnectionId(event.peer)});
          break;
        }
        case ENET_EVENT_TYPE_DISCONNECT: {
          events.push_back({.kind = TransportEventKind::kDisconnected, .connection = ReleaseConnectionId(event.peer)});
          break;
        }
        case ENET_EVENT_TYPE_RECEIVE: {
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
  ConnectionId next_connection_id_ = 1;
};

}  // namespace

std::expected<std::unique_ptr<Transport>, std::string> CreateEnetServerTransport(uint16_t port, size_t max_clients) {
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
  return std::make_unique<EnetTransport>(std::move(*lifetime), std::move(host));
}

std::expected<std::unique_ptr<Transport>, std::string> CreateEnetClientTransport(
    const Endpoint& server, z13::ConnectTimeoutConfig connect_timeout) {
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
  return std::make_unique<EnetTransport>(std::move(*lifetime), std::move(host));
}

}  // namespace z13::net
