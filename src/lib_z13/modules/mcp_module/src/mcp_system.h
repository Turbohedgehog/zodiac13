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
#include <memory>
#include <optional>

#include <flecs.h>

#include "mcp_server.h"

namespace z13::mcp {

// The running server, started by McpServerSystem when the settings ask for it. Runtime only, never state.
struct McpServerHandle {
  using Singleton = void;

  std::shared_ptr<McpServer> server;
  std::optional<std::uint32_t> failed_port;  // a port that did not bind: asked for again only when it changes
};

class McpServerSystem {
 public:
  static void Register(flecs::world& world);
};

}  // namespace z13::mcp
