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

#include <net_module/server_finder.h>

#include <algorithm>
#include <format>
#include <iterator>
#include <ranges>
#include <tuple>

#include <net_module/protocol.h>

namespace z13::net {

void ServerFinder::Record(const Endpoint& from, const ServerInfo& info, uint64_t now_ms) {
  const Endpoint game {.host = from.host, .port = info.game_port};
  FoundServer server {
      .endpoint = game,
      .name = info.name,
      .players = info.players,
      .ping_ms = now_ms >= info.sent_at_ms ? now_ms - info.sent_at_ms : 0,
  };
  if (!info.scene.empty()) {
    server.scene = info.scene;
  }
  if (info.protocol_version != kProtocolVersion) {
    server.incompatible = std::format("protocol {}, this game's is {}", info.protocol_version, kProtocolVersion);
  }
  heard_.insert_or_assign(game, HeardServer {.server = std::move(server), .heard_at_ms = now_ms});
}

void ServerFinder::Forget(uint64_t now_ms, uint64_t after_ms) {
  std::erase_if(heard_, [&](const auto& heard) { return now_ms - heard.second.heard_at_ms >= after_ms; });
}

std::vector<FoundServer> ServerFinder::Servers() const {
  std::vector<FoundServer> servers;
  std::ranges::transform(heard_ | std::views::values, std::back_inserter(servers),
                         [](const HeardServer& heard) { return heard.server; });
  std::ranges::sort(servers, {}, [](const FoundServer& server) {
    return std::tie(server.name, server.endpoint.host, server.endpoint.port);
  });
  return servers;
}

}  // namespace z13::net
