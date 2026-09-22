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

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <string>

#include <lib_core/config.h>
#include <lib_core/endpoint.h>

#include <net_module/transport.h>

namespace z13::net {

constexpr size_t kMaxClients = 32;

// Binds and listens on `port`; error if the port is taken or ENet fails to init.
std::expected<std::unique_ptr<Transport>, std::string> CreateEnetServerTransport(
    uint16_t port, size_t max_clients = kMaxClients);

// Starts an outgoing (non-blocking) connection to `server`; the outcome is a
// kConnected/kDisconnected event from the first Service() call, not the return
// value -- mirrors ENet's own async connect.
std::expected<std::unique_ptr<Transport>, std::string> CreateEnetClientTransport(
    const Endpoint& server, z13::ConnectTimeoutConfig connect_timeout = {});

}  // namespace z13::net
