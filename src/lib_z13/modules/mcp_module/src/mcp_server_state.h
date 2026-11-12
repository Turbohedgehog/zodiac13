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
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <boost/asio.hpp>

#include <lib_core/utils/status.h>

#include "mcp_server.h"
#include "mcp_session.h"

namespace z13::mcp {

// What the server's thread and the game thread share. The sessions are the thread's; the queue is locked.
struct McpServerState {
  Status Listen(std::uint16_t port);

  void Accept();

  boost::asio::io_context io;
  boost::asio::ip::tcp::acceptor acceptor {io};
  std::map<std::uint64_t, std::shared_ptr<McpSession>> sessions;
  std::uint16_t port {};  // the bound one, which differs from the asked when that was 0
  std::uint64_t next_session {1};
  std::mutex mutex;
  std::vector<McpCall> calls;
  std::thread thread;
};

}  // namespace z13::mcp
