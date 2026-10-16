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

#include <net_module/discovery_socket.h>

#include <array>
#include <utility>

#include <enet/enet.h>

#include "enet_datagram.h"
#include "enet_lifetime.h"

namespace z13::net {

namespace {

// Discovery messages are a few hundred bytes; anything longer isn't one.
constexpr size_t kMaxDatagram = 1024;

ENetSocket Native(uint64_t socket) {
  return static_cast<ENetSocket>(socket);
}

}  // namespace

std::expected<DiscoverySocket, std::string> DiscoverySocket::Open() {
  auto lifetime = AcquireEnetLifetime();
  if (!lifetime) {
    return std::unexpected(lifetime.error());
  }
  const ENetSocket socket = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
  if (socket == ENET_SOCKET_NULL) {
    return std::unexpected(std::string("can't create a UDP socket"));
  }
  DiscoverySocket opened(std::move(*lifetime), static_cast<uint64_t>(socket));
  enet_socket_set_option(socket, ENET_SOCKOPT_NONBLOCK, 1);
  enet_socket_set_option(socket, ENET_SOCKOPT_BROADCAST, 1);
  const ENetAddress address {.host = ENET_HOST_ANY, .port = ENET_PORT_ANY};
  if (enet_socket_bind(socket, &address) != 0) {
    return std::unexpected(std::string("can't bind a UDP port"));
  }
  return opened;
}

DiscoverySocket::DiscoverySocket(std::shared_ptr<EnetLifetime> lifetime, uint64_t socket)
    : lifetime_(std::move(lifetime)), socket_(socket), open_(true) {}

DiscoverySocket::DiscoverySocket(DiscoverySocket&& other) noexcept
    : lifetime_(std::move(other.lifetime_)), socket_(other.socket_), open_(std::exchange(other.open_, false)) {}

DiscoverySocket& DiscoverySocket::operator=(DiscoverySocket&& other) noexcept {
  if (this != &other) {
    Close();
    lifetime_ = std::move(other.lifetime_);
    socket_ = other.socket_;
    open_ = std::exchange(other.open_, false);
  }
  return *this;
}

DiscoverySocket::~DiscoverySocket() {
  Close();
}

void DiscoverySocket::Close() {
  if (open_) {
    enet_socket_destroy(Native(socket_));
    open_ = false;
  }
}

uint16_t DiscoverySocket::Port() const {
  ENetAddress address {};
  return enet_socket_get_address(Native(socket_), &address) == 0 ? address.port : 0;
}

Status DiscoverySocket::SendTo(const Endpoint& to, std::span<const uint8_t> bytes) const {
  return SendDatagram(Native(socket_), to, bytes);
}

std::vector<Datagram> DiscoverySocket::Receive() const {
  std::vector<Datagram> received;
  std::array<uint8_t, kMaxDatagram> data {};
  while (true) {
    ENetAddress from {};
    ENetBuffer buffer {};
    buffer.data = data.data();
    buffer.dataLength = data.size();
    const int length = enet_socket_receive(Native(socket_), &from, &buffer, 1);
    if (length <= 0) {
      return received;
    }
    received.push_back({.from = EndpointOf(from), .bytes = {data.begin(), data.begin() + length}});
  }
}

}  // namespace z13::net
