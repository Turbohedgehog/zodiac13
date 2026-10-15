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

#include "lan_discovery_system.h"

#include <chrono>
#include <memory>
#include <utility>

#include <flecs.h>

#include <lib_core/state/world_state.h>
#include <lib_core/utils/log.h>
#include <lib_core/world/lifecycle.h>
#include <net_module/discovery_messages.h>
#include <net_module/protocol.h>
#include <z13/components/net.h>
#include <z13/components/player.h>
#include <z13/components/station_mode.h>
#include <z13_settings/settings.h>

namespace z13::net {

namespace {

uint64_t NowMs() {
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

ServerInfo InfoOf(flecs::world world, const LanDiscoverySettings& settings, uint16_t game_port) {
  ServerInfo info {
      .name = settings.server_name,
      .protocol_version = kProtocolVersion,
      .players = static_cast<uint32_t>(world.count<z13::gameplay::Player>()),
      .game_port = game_port,
  };
  const auto* choice = world.try_get<z13::station::StationSceneChoice>();
  if (world.has<z13::station::StationMode>() && choice != nullptr && choice->scene) {
    info.scene = *choice->scene;
  }
  return info;
}

void Answer(flecs::world world, LanDiscoveryState& state, const LanDiscoverySettings& settings, uint64_t now_ms) {
  const auto* role = world.try_get<ServerRole>();
  if (role == nullptr) {
    state.server.reset();
    state.server_failed = false;
    return;
  }
  if (!state.server && !state.server_failed) {
    auto socket = DiscoverySocket::Open(static_cast<uint16_t>(settings.port), false);
    if (!socket) {
      log_warn("LAN discovery: {}; this server can still be joined by its address", socket.error());
      state.server_failed = true;
      return;
    }
    state.server.emplace(std::move(*socket));
  }
  if (!state.server) {
    return;
  }
  const ServerInfo info = InfoOf(world, settings, role->port);
  for (const Datagram& query : state.server->Receive()) {
    if (!state.limiter.Allow(query.from, now_ms, settings.reply_interval_ms)) {
      continue;
    }
    // A lost answer is asked again by the next query.
    if (const auto reply = ReplyTo(query.bytes, info)) {
      (void)state.server->SendTo(query.from, *reply);
    }
  }
}

void Look(flecs::world world, LanDiscoveryState& state, const LanDiscoverySettings& settings, uint64_t now_ms) {
  if (!world.has<LanSearch>()) {
    if (state.client || state.client_failed) {
      state.client.reset();
      state.client_failed = false;
      state.finder = {};
      world.set<FoundServers>({});
    }
    return;
  }
  if (!state.client && !state.client_failed) {
    auto socket = DiscoverySocket::Open(0, true);
    if (!socket) {
      log_warn("LAN discovery: {}; join by address", socket.error());
      state.client_failed = true;
      return;
    }
    state.client.emplace(std::move(*socket));
    state.next_query_ms = now_ms;
  }
  if (!state.client) {
    return;
  }
  if (now_ms >= state.next_query_ms) {
    const std::vector<uint8_t> query = EncodeQuery(now_ms);
    const auto port = static_cast<uint16_t>(settings.port);
    // Unanswered queries are sent again next interval; a network without broadcast still has the loopback.
    (void)state.client->SendTo({.host = std::string(kBroadcastHost), .port = port}, query);
    (void)state.client->SendTo({.host = std::string(kLoopbackHost), .port = port}, query);
    state.next_query_ms = now_ms + settings.query_interval_ms;
  }
  for (const Datagram& answer : state.client->Receive()) {
    const auto message = DecodeDiscovery(answer.bytes);
    if (const auto* info = message ? message->body.AsServerInfo() : nullptr) {
      state.finder.Record(answer.from, *info, now_ms);
    }
  }
  state.finder.Forget(now_ms, settings.forget_after_ms);
  std::vector<FoundServer> servers = state.finder.Servers();
  if (servers != world.get<FoundServers>().servers) {
    world.set<FoundServers>({.servers = std::move(servers)});
  }
}

void RegisterComponents(flecs::world world) {
  z13::flecs_tools::RegisterComponents<LanSearch, FoundServers, z13::LanDiscoverySettings>(world);
}

void RegisterSystems(flecs::world world) {
  // The launcher overwrites this with the loaded settings (z13::InstallSettings).
  world.set<z13::LanDiscoverySettings>({});
  world.set<FoundServers>({});
  const auto state = std::make_shared<LanDiscoveryState>();
  world.system<const z13::LanDiscoverySettings>("LanDiscoverySystem::Poll")
      .kind(flecs::OnUpdate)
      .read<ServerRole>()
      .read<LanSearch>()
      .each([state](flecs::iter& it, size_t, const z13::LanDiscoverySettings& settings) {
        const uint64_t now_ms = NowMs();
        Answer(it.world(), *state, settings, now_ms);
        Look(it.world(), *state, settings, now_ms);
      });
}

}  // namespace

void LanDiscoverySystem::Register(flecs::world& world) {
  OnRegisterComponents(world, RegisterComponents);
  OnInitSystems(world, RegisterSystems);
}

}  // namespace z13::net
