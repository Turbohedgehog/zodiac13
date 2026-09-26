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

#include <net_module/in_memory_transport.h>

#include <format>
#include <optional>
#include <random>
#include <unordered_map>
#include <utility>

namespace z13::net {

using TransportId = uint32_t;
constexpr TransportId kInvalidTransportId = 0;

struct PeerLink {
  TransportId remote_transport_id = kInvalidTransportId;
  ConnectionId remote_connection_id = kInvalidConnectionId;
};

namespace {

struct PendingConnect {
  uint64_t deliver_at_tick {};
  TransportId initiator = kInvalidTransportId;
  TransportId listener = kInvalidTransportId;  // kInvalidTransportId: no such port, initiator times out
};
struct PendingPacket {
  uint64_t deliver_at_tick {};
  TransportId destination = kInvalidTransportId;
  ConnectionId destination_connection = kInvalidConnectionId;
  Channel channel {};
  std::vector<std::byte> data;
};
struct PendingDisconnect {
  uint64_t deliver_at_tick {};
  TransportId destination = kInvalidTransportId;
  ConnectionId destination_connection = kInvalidConnectionId;
};

// Partitions out every entry due at or before `now`, in place.
template <typename T>
std::vector<T> TakeDue(std::vector<T>& all, uint64_t now) {
  std::vector<T> due;
  std::vector<T> remaining;
  for (auto& item : all) {
    if (item.deliver_at_tick <= now) {
      due.push_back(std::move(item));
    } else {
      remaining.push_back(std::move(item));
    }
  }
  all = std::move(remaining);
  return due;
}

}  // namespace

class InMemoryTransport;  // Defined below; this class only ever stores pointers to it.

// InMemoryNetwork's state, named and standalone (not a nested, friended "Impl") so
// InMemoryTransport and the free functions below reach it through public methods.
class InMemoryNetworkState {
 public:
  explicit InMemoryNetworkState(uint64_t seed) : rng_(seed) {}

  void SetFaultConfig(FaultConfig config) { fault_config_ = config; }

  void Tick();  // Defined below, once InMemoryTransport (which it calls into) exists.

  bool HasListener(uint16_t port) const { return listeners_.contains(port); }
  TransportId FindListener(uint16_t port) const {
    const auto it = listeners_.find(port);
    return it != listeners_.end() ? it->second : kInvalidTransportId;
  }
  void RegisterListener(uint16_t port, TransportId id) { listeners_[port] = id; }
  void UnregisterListener(uint16_t port) { listeners_.erase(port); }

  TransportId AllocateTransportId() { return next_transport_id_++; }
  void RegisterTransport(TransportId id, InMemoryTransport* transport) { transports_[id] = transport; }
  void UnregisterTransport(TransportId id) { transports_.erase(id); }

  void QueueConnect(TransportId initiator, TransportId listener) {
    pending_connects_.push_back(
        {.deliver_at_tick = current_tick_ + SampleDelayTicks(), .initiator = initiator, .listener = listener});
  }

  void QueuePacket(
      TransportId destination, ConnectionId destination_connection, Channel channel,
      std::vector<std::byte> data) {
    // Reliable traffic is never dropped, matching ENet: a lost packet there is
    // retransmitted, so it arrives late rather than not at all. Only the unreliable
    // channel (Ping/Pong) can actually go missing.
    if (channel == Channel::kUnreliable && RollDrop()) {
      return;
    }
    pending_packets_.push_back({
        .deliver_at_tick = current_tick_ + SampleDelayTicks(),
        .destination = destination,
        .destination_connection = destination_connection,
        .channel = channel,
        .data = std::move(data),
    });
  }

  void QueueDisconnect(TransportId destination, ConnectionId destination_connection) {
    pending_disconnects_.push_back({
        .deliver_at_tick = current_tick_,
        .destination = destination,
        .destination_connection = destination_connection,
    });
  }

 private:
  // Sampled for both connect scheduling and regular packets, so a fixed FaultConfig
  // makes connects arrive with the same jitter as the data that follows them.
  uint32_t SampleDelayTicks() {
    if (fault_config_.max_delay_ticks == 0) {
      return 0;
    }
    std::uniform_int_distribution<uint32_t> delay(fault_config_.min_delay_ticks, fault_config_.max_delay_ticks);
    return delay(rng_);
  }

  bool RollDrop() {
    if (fault_config_.drop_probability <= 0.0) {
      return false;
    }
    std::uniform_real_distribution<double> roll(0.0, 1.0);
    return roll(rng_) < fault_config_.drop_probability;
  }

  uint64_t current_tick_ {};
  TransportId next_transport_id_ = 1;
  ConnectionId next_connection_id_ = 1;
  FaultConfig fault_config_;
  std::mt19937_64 rng_;

  std::unordered_map<uint16_t, TransportId> listeners_;
  std::unordered_map<TransportId, InMemoryTransport*> transports_;  // live transports only

  std::vector<PendingConnect> pending_connects_;
  std::vector<PendingPacket> pending_packets_;
  std::vector<PendingDisconnect> pending_disconnects_;
};

// Stays at z13::net scope, matching the forward declaration above (an anonymous-
// namespace definition would be a different, incompatible type); never named outside
// this file otherwise -- only the abstract Transport* crosses to callers.
class InMemoryTransport final : public Transport {
 public:
  InMemoryTransport(std::shared_ptr<InMemoryNetworkState> network, TransportId id)
      : network_(std::move(network)), id_(id) {}

  ~InMemoryTransport() override {
    if (listening_port_) {
      network_->UnregisterListener(*listening_port_);
    }
    network_->UnregisterTransport(id_);
  }

  void Send(ConnectionId connection, Channel channel, std::span<const std::byte> data) override {
    const auto it = peers_.find(connection);
    if (it == peers_.end()) {
      return;
    }
    network_->QueuePacket(
        it->second.remote_transport_id, it->second.remote_connection_id, channel,
        std::vector<std::byte>(data.begin(), data.end()));
  }

  void Disconnect(ConnectionId connection) override {
    const auto it = peers_.find(connection);
    if (it == peers_.end()) {
      return;
    }
    network_->QueueDisconnect(it->second.remote_transport_id, it->second.remote_connection_id);
    peers_.erase(it);
  }

  std::vector<TransportEvent> Service() override { return std::exchange(inbox_, {}); }

  void SetPeer(ConnectionId local_id, PeerLink link) { peers_[local_id] = link; }
  void QueueEvent(TransportEvent event) { inbox_.push_back(std::move(event)); }
  // Lets the destructor free the port so a later transport can rebind it.
  void SetListeningPort(uint16_t port) { listening_port_ = port; }

 private:
  std::shared_ptr<InMemoryNetworkState> network_;
  TransportId id_;
  std::optional<uint16_t> listening_port_;
  std::unordered_map<ConnectionId, PeerLink> peers_;
  std::vector<TransportEvent> inbox_;
};

void InMemoryNetworkState::Tick() {
  ++current_tick_;
  const uint64_t now = current_tick_;

  for (auto& pending : TakeDue(pending_connects_, now)) {
    const auto initiator_it = transports_.find(pending.initiator);
    if (initiator_it == transports_.end()) {
      continue;  // initiator already destroyed, nothing to notify
    }
    InMemoryTransport* listener = nullptr;
    if (pending.listener != kInvalidTransportId) {
      const auto listener_it = transports_.find(pending.listener);
      if (listener_it != transports_.end()) {
        listener = listener_it->second;
      }
    }
    if (!listener) {
      initiator_it->second->QueueEvent({.kind = TransportEventKind::kDisconnected});
      continue;
    }
    const ConnectionId listener_side_id = next_connection_id_++;
    const ConnectionId initiator_side_id = next_connection_id_++;
    listener->SetPeer(listener_side_id, {pending.initiator, initiator_side_id});
    initiator_it->second->SetPeer(initiator_side_id, {pending.listener, listener_side_id});
    listener->QueueEvent({.kind = TransportEventKind::kConnected, .connection = listener_side_id});
    initiator_it->second->QueueEvent({.kind = TransportEventKind::kConnected, .connection = initiator_side_id});
  }

  for (auto& pending : TakeDue(pending_packets_, now)) {
    const auto it = transports_.find(pending.destination);
    if (it == transports_.end()) {
      continue;
    }
    it->second->QueueEvent({
        .kind = TransportEventKind::kReceived,
        .connection = pending.destination_connection,
        .channel = pending.channel,
        .data = std::move(pending.data),
    });
  }

  for (auto& pending : TakeDue(pending_disconnects_, now)) {
    const auto it = transports_.find(pending.destination);
    if (it == transports_.end()) {
      continue;
    }
    it->second->QueueEvent({.kind = TransportEventKind::kDisconnected, .connection = pending.destination_connection});
  }
}

InMemoryNetwork::InMemoryNetwork(uint64_t seed) : state_(std::make_shared<InMemoryNetworkState>(seed)) {}

void InMemoryNetwork::SetFaultConfig(FaultConfig config) {
  state_->SetFaultConfig(config);
}

void InMemoryNetwork::Tick() {
  state_->Tick();
}

std::expected<std::unique_ptr<Transport>, std::string> CreateInMemoryServerTransport(
    InMemoryNetwork& network, uint16_t port) {
  auto& state = *network.GetState();
  if (state.HasListener(port)) {
    return std::unexpected(std::format("port {} already has a listener on this InMemoryNetwork", port));
  }

  const TransportId id = state.AllocateTransportId();
  auto transport = std::make_unique<InMemoryTransport>(network.GetState(), id);
  state.RegisterTransport(id, transport.get());
  state.RegisterListener(port, id);
  transport->SetListeningPort(port);
  return transport;
}

std::unique_ptr<Transport> CreateInMemoryClientTransport(InMemoryNetwork& network, uint16_t server_port) {
  auto& state = *network.GetState();

  const TransportId id = state.AllocateTransportId();
  auto transport = std::make_unique<InMemoryTransport>(network.GetState(), id);
  state.RegisterTransport(id, transport.get());

  state.QueueConnect(id, state.FindListener(server_port));
  return transport;
}

}  // namespace z13::net
