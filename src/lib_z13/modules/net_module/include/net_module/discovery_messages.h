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
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <discovery_generated.h>

namespace z13::net {

// The discovery format (discovery.fbs); a query of another is dropped unanswered.
inline constexpr uint32_t kDiscoveryFormatVersion = 2;

using DiscoveryMessage = fbs::discovery::DiscoveryMessageT;
using ServerInfo = fbs::discovery::ServerInfoT;

std::vector<uint8_t> EncodeDiscovery(const DiscoveryMessage& message);
// Verified first: anyone on the network can send it.
std::expected<DiscoveryMessage, std::string> DecodeDiscovery(std::span<const uint8_t> bytes);
// Tells a discovery message from ENet's datagrams on a server's port.
bool IsDiscoveryMessage(std::span<const uint8_t> bytes);

std::vector<uint8_t> EncodeQuery(uint64_t sent_at_ms);
// What a server sends back to `query`: `info` with the query's time; nothing to a message of
// another format or kind.
std::optional<std::vector<uint8_t>> ReplyTo(std::span<const uint8_t> query, ServerInfo info);

}  // namespace z13::net
