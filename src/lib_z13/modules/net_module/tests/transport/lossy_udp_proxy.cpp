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

#include "lossy_udp_proxy.h"

#include <array>
#include <format>

#include "../../src/enet_datagram.h"
#include "../../src/enet_lifetime.h"

namespace z13::net {

namespace {

// Larger than ENet's biggest datagram (its MTU maximum is 4096).
constexpr size_t kMaxDatagram = 4096;

}  // namespace

std::expected<std::unique_ptr<LossyUdpProxy>, std::string> LossyUdpProxy::Create(const LossyProxyConfig& config) {
  auto lifetime = AcquireEnetLifetime();
  if (!lifetime) {
    return std::unexpected(lifetime.error());
  }
  auto proxy = std::unique_ptr<LossyUdpProxy>(new LossyUdpProxy(config));
  proxy->lifetime_ = *lifetime;
  proxy->front_ = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
  proxy->back_ = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
  if (proxy->front_ == ENET_SOCKET_NULL || proxy->back_ == ENET_SOCKET_NULL) {
    return std::unexpected("can't create the proxy's sockets");
  }
  ENetAddress address {.host = ENET_HOST_ANY, .port = config.port};
  if (enet_socket_bind(proxy->front_, &address) != 0) {
    return std::unexpected(std::format("can't bind the proxy to port {}", config.port));
  }
  constexpr int kNonBlocking = 1;
  enet_socket_set_option(proxy->front_, ENET_SOCKOPT_NONBLOCK, kNonBlocking);
  enet_socket_set_option(proxy->back_, ENET_SOCKOPT_NONBLOCK, kNonBlocking);
  return proxy;
}

LossyUdpProxy::LossyUdpProxy(const LossyProxyConfig& config) : config_(config), random_(config.seed) {}

LossyUdpProxy::~LossyUdpProxy() {
  enet_socket_destroy(front_);
  enet_socket_destroy(back_);
}

void LossyUdpProxy::Take(ENetSocket socket, ProxyDirection direction) {
  std::array<uint8_t, kMaxDatagram> storage {};
  ENetBuffer buffer {};  // its fields come in another order on Windows: assigned, not designated
  buffer.data = storage.data();
  buffer.dataLength = storage.size();
  ENetAddress from {};
  for (int received = enet_socket_receive(socket, &from, &buffer, 1); received > 0;
       received = enet_socket_receive(socket, &from, &buffer, 1)) {
    if (direction == ProxyDirection::kToServer) {
      client_ = from;
    }
    if (std::uniform_real_distribution<double>(0., 1.)(random_) < config_.loss) {
      ++dropped_;
      continue;
    }
    queue_.push_back({std::chrono::steady_clock::now() + config_.one_way_delay, direction,
                      {storage.begin(), storage.begin() + received}});
  }
}

void LossyUdpProxy::Release() {
  const auto now = std::chrono::steady_clock::now();
  for (; !queue_.empty() && queue_.front().due <= now; queue_.pop_front()) {
    const DelayedDatagram& datagram = queue_.front();
    if (datagram.direction == ProxyDirection::kToServer) {
      SendDatagram(back_, config_.server, datagram.bytes);
    } else if (client_) {
      SendDatagram(front_, EndpointOf(*client_), datagram.bytes);
    }
    ++forwarded_;
  }
}

void LossyUdpProxy::Pump() {
  Take(front_, ProxyDirection::kToServer);
  Take(back_, ProxyDirection::kToClient);
  Release();
}

}  // namespace z13::net
