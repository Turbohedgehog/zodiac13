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

#include <chrono>
#include <cstdint>
#include <deque>
#include <expected>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include <lib_core/utils/endpoint.h>
#include <net_module/discovery_socket.h>

namespace z13::net {

struct LossyProxyConfig {
  Endpoint server;
  double loss {};  // chance that a datagram is dropped, either way
  std::chrono::milliseconds one_way_delay {};
  uint32_t seed {};
};

enum class ProxyDirection { kToServer, kToClient };

struct DelayedDatagram {
  std::chrono::steady_clock::time_point due;
  ProxyDirection direction {};
  std::vector<uint8_t> bytes;
};

// A UDP relay between one ENet client and a server on loopback that drops and delays datagrams, to see
// what ENet's reliable channel does on a lossy link. Pump() it in the loop that services the transports.
class LossyUdpProxy {
 public:
  static std::expected<std::unique_ptr<LossyUdpProxy>, std::string> Create(const LossyProxyConfig& config);
  LossyUdpProxy(const LossyUdpProxy&) = delete;
  LossyUdpProxy& operator=(const LossyUdpProxy&) = delete;

  uint16_t Port() const { return front_.Port(); }
  void Pump();
  uint64_t Dropped() const { return dropped_; }
  uint64_t Forwarded() const { return forwarded_; }

 private:
  LossyUdpProxy(const LossyProxyConfig& config, DiscoverySocket front, DiscoverySocket back);

  void Take(const DiscoverySocket& socket, ProxyDirection direction);
  void Release();

  LossyProxyConfig config_;
  DiscoverySocket front_;
  DiscoverySocket back_;
  std::optional<Endpoint> client_;
  std::deque<DelayedDatagram> queue_;
  std::mt19937 random_;
  uint64_t dropped_ {};
  uint64_t forwarded_ {};
};

}  // namespace z13::net
