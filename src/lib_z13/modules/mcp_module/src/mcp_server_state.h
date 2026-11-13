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

#include <atomic>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <boost/asio.hpp>

#include <lib_core/utils/status.h>

#include "mcp_protocol.h"
#include "mcp_server.h"
#include "mcp_session.h"

namespace z13::mcp {

// The built-in request the server's own thread answers, without the game thread.
constexpr std::string_view kServerStatusCommand = "server_status";

// The built-in request that makes the session a receiver of the server's events (McpServer::Publish).
constexpr std::string_view kSubscribeCommand = "subscribe";

struct ServerStatus {
  std::uint64_t pumps {};
  std::size_t queued_calls {};
  std::optional<std::int64_t> ms_since_pump;  // absent until the first pump
};

// What the server's thread and the game thread share. The sessions are the thread's; the queue is locked.
struct McpServerState {
  Status Listen(std::uint16_t port);

  void Accept();

  CommandResult StatusOf();

  // Sends the line to every subscribed session; on the server's thread.
  void Publish(const std::string& line);

  boost::asio::io_context io;
  boost::asio::ip::tcp::acceptor acceptor {io};
  std::map<std::uint64_t, std::shared_ptr<McpSession>> sessions;
  std::set<std::uint64_t> subscribers;
  // The count of subscribers, for the game thread.
  std::atomic<std::size_t> subscribed {};
  std::uint16_t port {};  // the bound one, which differs from the asked when that was 0
  std::uint64_t next_session {1};
  std::mutex mutex;
  std::vector<McpCall> calls;
  // Read by server_status: tells a frozen game from a broken server.
  std::atomic<std::uint64_t> pumps {};
  std::atomic<std::chrono::steady_clock::rep> last_pump {};
  std::thread thread;
};

}  // namespace z13::mcp
