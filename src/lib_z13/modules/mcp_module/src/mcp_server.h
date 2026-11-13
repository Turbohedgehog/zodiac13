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
#include <vector>

#include "mcp_protocol.h"

namespace z13::mcp {

struct McpCall {
  std::uint64_t session {};
  McpRequest request;
};

struct McpServerState;

// A TCP server on 127.0.0.1 that reads requests on a thread of its own: the game thread takes them with
// TakeCalls and answers with Reply. A line that is not a request is answered with an error right there.
class McpServer {
 public:
  // Port 0 lets the system pick one (see Port).
  static std::expected<std::shared_ptr<McpServer>, std::string> Start(std::uint16_t port);

  ~McpServer();
  McpServer(const McpServer&) = delete;
  McpServer& operator=(const McpServer&) = delete;

  std::uint16_t Port() const;
  std::vector<McpCall> TakeCalls();
  void NotePump();
  void Reply(std::uint64_t session, std::string line);
  // Sends a line to the clients that sent `subscribe`, whenever they connected.
  void Publish(std::string line);
  std::size_t Subscribers() const;

 private:
  explicit McpServer(std::shared_ptr<McpServerState> state);

  std::shared_ptr<McpServerState> state_;
};

}  // namespace z13::mcp
