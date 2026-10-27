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

#include <utility>

namespace z13::net {

std::expected<std::unique_ptr<LossyUdpProxy>, std::string> LossyUdpProxy::Create(const LossyProxyConfig& config) {
  auto front = DiscoverySocket::Open();
  auto back = DiscoverySocket::Open();
  if (!front || !back) {
    return std::unexpected(front ? back.error() : front.error());
  }
  return std::unique_ptr<LossyUdpProxy>(new LossyUdpProxy(config, std::move(*front), std::move(*back)));
}

LossyUdpProxy::LossyUdpProxy(const LossyProxyConfig& config, DiscoverySocket front, DiscoverySocket back)
    : config_(config), front_(std::move(front)), back_(std::move(back)), random_(config.seed) {}

void LossyUdpProxy::Take(const DiscoverySocket& socket, ProxyDirection direction) {
  for (Datagram& datagram : socket.Receive()) {
    if (direction == ProxyDirection::kToServer) {
      client_ = datagram.from;
    }
    if (std::uniform_real_distribution<double>(0., 1.)(random_) < config_.loss) {
      ++dropped_;
      continue;
    }
    queue_.push_back({std::chrono::steady_clock::now() + config_.one_way_delay, direction, std::move(datagram.bytes)});
  }
}

void LossyUdpProxy::Release() {
  const auto now = std::chrono::steady_clock::now();
  for (; !queue_.empty() && queue_.front().due <= now; queue_.pop_front()) {
    const DelayedDatagram& datagram = queue_.front();
    if (datagram.direction == ProxyDirection::kToServer) {
      back_.SendTo(config_.server, datagram.bytes);
    } else if (client_) {
      front_.SendTo(*client_, datagram.bytes);
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
