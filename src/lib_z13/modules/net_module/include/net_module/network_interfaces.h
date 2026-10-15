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
#include <span>
#include <string>
#include <vector>

namespace z13::net {

struct InterfaceAddress {
  // Host byte order.
  uint32_t address {};
  uint8_t prefix_length {};
};

// The IPv4 addresses of this machine's interfaces that are up, the loopback left out.
std::expected<std::vector<InterfaceAddress>, std::string> ListInterfaceAddresses();

// Where a query reaches every subnet: the OS sends kBroadcastHost through one interface only, so
// each subnet's own broadcast address goes with it.
std::vector<std::string> BroadcastHosts(std::span<const InterfaceAddress> interfaces);

}  // namespace z13::net
