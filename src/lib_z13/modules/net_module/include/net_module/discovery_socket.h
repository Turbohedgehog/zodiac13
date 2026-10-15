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

#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include <lib_core/utils/endpoint.h>
#include <lib_core/utils/status.h>

namespace z13::net {

class EnetLifetime;

struct Datagram {
  Endpoint from;
  std::vector<uint8_t> bytes;
};

// A non-blocking UDP socket for finding servers (ENet's, no other dependency), polled once a
// frame. Move-only: it closes its socket.
class DiscoverySocket {
 public:
  // Bound on every interface to `port`, or any free port for 0; `broadcast` lets it send to
  // kBroadcastHost.
  static std::expected<DiscoverySocket, std::string> Open(uint16_t port, bool broadcast);

  DiscoverySocket(DiscoverySocket&& other) noexcept;
  DiscoverySocket& operator=(DiscoverySocket&& other) noexcept;
  DiscoverySocket(const DiscoverySocket&) = delete;
  DiscoverySocket& operator=(const DiscoverySocket&) = delete;
  ~DiscoverySocket();

  uint16_t Port() const;
  Status SendTo(const Endpoint& to, std::span<const uint8_t> bytes) const;
  // Everything that arrived since the last call.
  std::vector<Datagram> Receive() const;

 private:
  DiscoverySocket(std::shared_ptr<EnetLifetime> lifetime, uint64_t socket);
  void Close();

  std::shared_ptr<EnetLifetime> lifetime_;
  // ENetSocket, a descriptor on every platform; kept as an integer so enet.h stays out of here.
  uint64_t socket_ {};
  bool open_ {};
};

// Every host of the local network.
inline constexpr std::string_view kBroadcastHost = "255.255.255.255";
// This machine: a server started from its own menu is found too.
inline constexpr std::string_view kLoopbackHost = "127.0.0.1";

}  // namespace z13::net
