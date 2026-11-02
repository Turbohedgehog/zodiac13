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

#include <net_module/network_interfaces.h>

#include <algorithm>
#include <bit>
#include <format>
#include <ranges>

#include <uv.h>

#include <net_module/discovery_socket.h>

namespace z13::net {

namespace {

constexpr uint32_t kAllBits = 0xFFFFFFFFu;
// A /31 or /32 link has no broadcast address.
constexpr uint8_t kMaxBroadcastPrefix = 30;
constexpr int kOctetBits = 8;
constexpr uint32_t kOctetMask = 0xFFu;

uint32_t HostOrder(uint32_t network_order) {
  return std::endian::native == std::endian::little ? std::byteswap(network_order) : network_order;
}

uint32_t Octet(uint32_t address, int index) {
  return (address >> (index * kOctetBits)) & kOctetMask;
}

std::string HostText(uint32_t address) {
  return std::format("{}.{}.{}.{}", Octet(address, 3), Octet(address, 2), Octet(address, 1), Octet(address, 0));
}

}  // namespace

std::expected<std::vector<InterfaceAddress>, std::string> ListInterfaceAddresses() {
  uv_interface_address_t* listed {};
  int count {};
  if (const int error = uv_interface_addresses(&listed, &count); error != 0) {
    return std::unexpected(std::format("can't list the network interfaces: {}", uv_strerror(error)));
  }
  std::vector<InterfaceAddress> found;
  // libuv lists only the interfaces that are up; is_internal is the loopback.
  for (const uv_interface_address_t& entry : std::span(listed, static_cast<size_t>(count))) {
    if (entry.is_internal != 0 || entry.address.address4.sin_family != AF_INET) {
      continue;
    }
    const uint32_t mask = HostOrder(entry.netmask.netmask4.sin_addr.s_addr);
    found.push_back({
        .address = HostOrder(entry.address.address4.sin_addr.s_addr),
        .prefix_length = static_cast<uint8_t>(std::popcount(mask)),
    });
  }
  uv_free_interface_addresses(listed, count);
  return found;
}

std::vector<std::string> BroadcastHosts(std::span<const InterfaceAddress> interfaces) {
  std::vector<std::string> hosts {std::string(kBroadcastHost)};
  const auto has_broadcast = [](const InterfaceAddress& entry) { return entry.prefix_length <= kMaxBroadcastPrefix; };
  for (const InterfaceAddress& entry : interfaces | std::views::filter(has_broadcast)) {
    hosts.push_back(HostText(entry.address | (kAllBits >> entry.prefix_length)));
  }
  std::ranges::sort(hosts);
  const auto duplicates = std::ranges::unique(hosts);
  hosts.erase(duplicates.begin(), duplicates.end());
  return hosts;
}

}  // namespace z13::net
