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

#include "enet_datagram.h"

#include <array>
#include <expected>
#include <format>
#include <string>

namespace z13::net {

namespace {

// An IPv4 address in text, its terminator included.
constexpr size_t kMaxHostText = 16;

std::expected<ENetAddress, std::string> AddressOf(const Endpoint& endpoint) {
  ENetAddress address {.host = ENET_HOST_ANY, .port = endpoint.port};
  if (enet_address_set_host_ip(&address, endpoint.host.c_str()) != 0) {
    return std::unexpected(std::format("'{}' isn't an IPv4 address", endpoint.host));
  }
  return address;
}

}  // namespace

Status SendDatagram(ENetSocket socket, const Endpoint& to, std::span<const uint8_t> bytes) {
  auto address = AddressOf(to);
  if (!address) {
    return std::unexpected(address.error());
  }
  ENetBuffer buffer {};  // its fields come in another order on Windows: assigned, not designated
  // ENet reads the data through a non-const pointer but doesn't change it.
  buffer.data = const_cast<uint8_t*>(bytes.data());
  buffer.dataLength = bytes.size();
  if (enet_socket_send(socket, &*address, &buffer, 1) < 0) {
    return std::unexpected(std::format("can't send to {}:{}", to.host, to.port));
  }
  return {};
}

Endpoint EndpointOf(const ENetAddress& address) {
  std::array<char, kMaxHostText> host {};
  enet_address_get_host_ip(&address, host.data(), host.size());
  return {.host = host.data(), .port = address.port};
}

}  // namespace z13::net
