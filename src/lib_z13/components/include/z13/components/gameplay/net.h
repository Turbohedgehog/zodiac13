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

#include <lib_core/utils/endpoint.h>

namespace z13::net {

// Runtime-only (not state) role tags, set by net_module while a session is open; neither
// present means single-player. Tags rather than an enum so a future composite role (listen-server) fits.
struct ServerRole {
  using Singleton = void;
  // The game's, which LAN discovery tells clients.
  uint16_t port {};
  // The port opens once the clock passes this tick: the scene is built by then.
  uint64_t listen_after_tick {};
};
struct ClientRole {
  using Singleton = void;
};

// One-shot requests (an entity per request) raised by BootstrapSystem for --server/--connect
// or by the main menu, consumed by net_module.
struct StartServerRequest {
  uint16_t port {};
};

struct JoinRequest {
  Endpoint endpoint;
};

// Abandons a join still connecting; leaving a running game is removing Gameplay.
struct LeaveRequest {};

// Present while the Join window looks for servers on the LAN.
struct LanSearch {
  using Singleton = void;
};

// A server found on the LAN (net_module's ServerFinder), as the Join window lists it.
struct FoundServer {
  // Where to join: the address it answered from, at its game port.
  Endpoint endpoint;
  std::string name;
  // The station blueprint; nothing for the spaceship.
  std::optional<std::string> scene;
  uint32_t players {};
  uint64_t ping_ms {};
  // Why it can't be joined (another protocol version); nothing if it can.
  std::optional<std::string> incompatible;

  bool operator==(const FoundServer&) const = default;
};

struct FoundServers {
  using Singleton = void;
  std::vector<FoundServer> servers;
};

// Not state -- rebuilt by net_module every session, never saved. net_module is the
// only writer; GUI reads it to show connecting/failure state.
enum class ConnectionState { kNone, kConnecting, kConnected, kFailed, kDisconnected };

struct ConnectionStatus {
  using Singleton = void;

  ConnectionState state = ConnectionState::kNone;
  std::string reason;  // populated for kFailed/kDisconnected, empty otherwise
};

}  // namespace z13::net
