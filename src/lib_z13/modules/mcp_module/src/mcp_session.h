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
#include <deque>
#include <functional>
#include <memory>
#include <string>

#include <boost/asio.hpp>

namespace z13::mcp {

// One client of the MCP server: reads lines and writes lines, on the server's thread only.
class McpSession : public std::enable_shared_from_this<McpSession> {
 public:
  using LineHandler = std::function<void(std::uint64_t session, std::string line)>;
  using ClosedHandler = std::function<void(std::uint64_t session)>;

  McpSession(boost::asio::ip::tcp::socket socket, std::uint64_t id, LineHandler on_line, ClosedHandler on_closed);

  void Start();
  void Write(std::string line);
  void Close();

 private:
  void ReadLine();
  void WriteNext();

  boost::asio::ip::tcp::socket socket_;
  std::uint64_t id_ {};
  LineHandler on_line_;
  ClosedHandler on_closed_;
  boost::asio::streambuf input_;
  std::deque<std::string> outbox_;
  bool writing_ {};
  bool closed_ {};
};

}  // namespace z13::mcp
