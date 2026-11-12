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


#include "mcp_server_state.h"

#include <format>
#include <utility>

namespace z13::mcp {

Status McpServerState::Listen(std::uint16_t port) {
  const boost::asio::ip::tcp::endpoint endpoint(boost::asio::ip::address_v4::loopback(), port);
  boost::system::error_code error;
  if (acceptor.open(endpoint.protocol(), error); error) {
    return std::unexpected(std::format("can't open a socket: {}", error.message()));
  }
  acceptor.set_option(boost::asio::socket_base::reuse_address(true), error);
  if (acceptor.bind(endpoint, error); error) {
    return std::unexpected(std::format("can't bind 127.0.0.1:{}: {}", port, error.message()));
  }
  if (acceptor.listen(boost::asio::socket_base::max_listen_connections, error); error) {
    return std::unexpected(std::format("can't listen on 127.0.0.1:{}: {}", port, error.message()));
  }
  this->port = acceptor.local_endpoint().port();
  return {};
}

void McpServerState::Accept() {
  acceptor.async_accept([this](const boost::system::error_code& error, boost::asio::ip::tcp::socket socket) {
    if (error == boost::asio::error::operation_aborted) {
      return;
    }
    if (!error) {
      const std::uint64_t id = next_session++;
      auto session = std::make_shared<McpSession>(
          std::move(socket), id,
          [this](std::uint64_t session_id, std::string line) {
            const auto request = ParseRequest(line);
            if (!request) {
              sessions.at(session_id)->Write(FormatReply(0, std::unexpected(request.error())));
              return;
            }
            const std::scoped_lock lock(mutex);
            calls.push_back({.session = session_id, .request = *request});
          },
          [this](std::uint64_t session_id) { sessions.erase(session_id); });
      sessions.emplace(id, session);
      session->Start();
    }
    Accept();
  });
}

}  // namespace z13::mcp
