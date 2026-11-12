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


#include "mcp_server.h"

#include <utility>

#include "mcp_server_state.h"

namespace z13::mcp {

std::expected<std::shared_ptr<McpServer>, std::string> McpServer::Start(std::uint16_t port) {
  auto state = std::make_shared<McpServerState>();
  if (const Status listening = state->Listen(port); !listening) {
    return std::unexpected(listening.error());
  }
  state->Accept();
  state->thread = std::thread([raw = state.get()] { raw->io.run(); });
  return std::shared_ptr<McpServer>(new McpServer(std::move(state)));
}

McpServer::McpServer(std::shared_ptr<McpServerState> state) : state_(std::move(state)) {}

McpServer::~McpServer() {
  state_->io.stop();
  state_->thread.join();
}

std::uint16_t McpServer::Port() const {
  return state_->port;
}

void McpServer::NotePump() {
  state_->pumps.fetch_add(1);
  state_->last_pump.store(std::chrono::steady_clock::now().time_since_epoch().count());
}

std::vector<McpCall> McpServer::TakeCalls() {
  const std::scoped_lock lock(state_->mutex);
  return std::exchange(state_->calls, {});
}

void McpServer::Reply(std::uint64_t session, std::string line) {
  boost::asio::post(state_->io, [state = state_.get(), session, text = std::move(line)]() mutable {
    if (const auto found = state->sessions.find(session); found != state->sessions.end()) {
      found->second->Write(std::move(text));
    }
  });
}

}  // namespace z13::mcp
