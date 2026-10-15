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
#include <optional>
#include <string>
#include <vector>

#include <lib_core/world/core_types.h>
#include <net_module/discovery_socket.h>
#include <net_module/reply_limiter.h>
#include <net_module/server_finder.h>

namespace z13::net {

// What LanDiscoverySystem's answers keep between frames; runtime only, never state.
struct LanAnswerState {
  ReplyLimiter limiter;
  // ServerInfo::instance_id.
  uint64_t instance_id {};
};

// What LanDiscoverySystem's search keeps between frames; runtime only, never state.
struct LanDiscoveryState {
  // Open while a Join window looks for servers (LanSearch).
  std::optional<DiscoverySocket> client;
  bool client_failed {};
  // Taken when the socket opens: reopening the Join window sees new interfaces.
  std::vector<std::string> broadcast_hosts;
  ServerFinder finder;
  uint64_t next_query_ms {};
};

// Finding servers on the LAN (docs/server-plan.md, "Поиск сервера"): a server answers on its
// game port the queries a Join window broadcasts, and the window lists the answers (FoundServers).
class LanDiscoverySystem {
 public:
  static void Register(flecs::world& world);
};

}  // namespace z13::net
