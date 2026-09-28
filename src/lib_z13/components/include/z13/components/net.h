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
#include <string>

#include <lib_core/endpoint.h>

namespace z13::net {

// Runtime-only (not state) role tags, set by net_module while a session is open; neither
// present means single-player. Tags rather than an enum so a future composite role (listen-server) fits.
struct ServerRole {
  using Singleton = void;
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

// Not state -- rebuilt by net_module every session, never saved. net_module is the
// only writer; GUI reads it to show connecting/failure state.
enum class ConnectionState { kNone, kConnecting, kConnected, kFailed, kDisconnected };

struct ConnectionStatus {
  using Singleton = void;

  ConnectionState state = ConnectionState::kNone;
  std::string reason;  // populated for kFailed/kDisconnected, empty otherwise
};

}  // namespace z13::net
