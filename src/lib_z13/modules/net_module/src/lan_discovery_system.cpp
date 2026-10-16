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
#include <random>
#include <utility>

#include <flecs.h>

#include <lib_core/settings/config.h>
#include <lib_core/state/world_state.h>
#include <lib_core/utils/log.h>
#include <lib_core/world/lifecycle.h>
#include <net_module/discovery_messages.h>
#include <net_module/network_interfaces.h>
#include <net_module/protocol.h>
#include <z13/components/net.h>
#include <z13/components/player.h>
#include <z13/components/station_mode.h>
#include <z13_settings/settings.h>

#include "net_session.h"

namespace z13::net {

namespace {

uint64_t NowMs() {
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

ServerInfo InfoOf(flecs::world world, const LanDiscoverySettings& settings, uint16_t game_port,
                  uint64_t instance_id) {
  ServerInfo info {
      .name = settings.server_name,
      .protocol_version = kProtocolVersion,
      .players = static_cast<uint32_t>(world.count<z13::gameplay::Player>()),
      .game_port = game_port,
      .instance_id = instance_id,
  };
  const auto* choice = world.try_get<z13::station::StationSceneChoice>();
  if (world.has<z13::station::StationMode>() && choice != nullptr && choice->scene) {
    info.scene = *choice->scene;
  }
  return info;
}

std::vector<std::string> LocalBroadcastHosts() {
  const auto interfaces = ListInterfaceAddresses();
  if (!interfaces) {
    log_warn("LAN discovery: {}; asking through the default interface only", interfaces.error());
    return {std::string(kBroadcastHost)};
  }
  return BroadcastHosts(*interfaces);
}

void Answer(flecs::world world, NetSession& session, LanAnswerState& state, uint16_t game_port,
            const LanDiscoverySettings& settings) {
  const std::vector<Datagram> queries = session.TakeDatagrams();
  if (queries.empty()) {
    return;
  }
  const uint64_t now_ms = NowMs();
  const ServerInfo info = InfoOf(world, settings, game_port, state.instance_id);
  for (const Datagram& query : queries) {
    if (!state.limiter.Allow(query.from, now_ms, settings.reply_interval_ms)) {
      continue;
    }
    // A lost answer is asked again by the next query.
    if (const auto reply = ReplyTo(query.bytes, info)) {
      (void)session.SendDatagram(query.from, *reply);
    }
  }
}

void Look(flecs::world world, LanDiscoveryState& state, const LanDiscoverySettings& settings) {
  const uint64_t now_ms = NowMs();
  if (!world.has<LanSearch>()) {
    if (state.client || state.client_failed) {
      state = {};
      world.set<FoundServers>({});
    }
    return;
  }
  if (!state.client && !state.client_failed) {
    auto socket = DiscoverySocket::Open();
    if (!socket) {
      log_warn("LAN discovery: {}; join by address", socket.error());
      state.client_failed = true;
      return;
    }
    state.client.emplace(std::move(*socket));
    state.broadcast_hosts = LocalBroadcastHosts();
    state.next_query_ms = now_ms;
  }
  if (!state.client) {
    return;
  }
  if (now_ms >= state.next_query_ms) {
    const std::vector<uint8_t> query = EncodeQuery(now_ms);
    // Unanswered queries are sent again next interval; a network without broadcast still has the loopback.
    for (const std::string& host : state.broadcast_hosts) {
      (void)state.client->SendTo({.host = host, .port = kDefaultServerPort}, query);
    }
    (void)state.client->SendTo({.host = std::string(kLoopbackHost), .port = kDefaultServerPort}, query);
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
  // PreFrame keeps running in the main menu, where the simulation is frozen (SimulationFrozen).
  std::mt19937_64 random(std::random_device {}());
  const auto answers = std::make_shared<LanAnswerState>(LanAnswerState {.instance_id = random()});
  world.system<NetSession, const ServerRole, const z13::LanDiscoverySettings>("LanDiscoverySystem::Answer")
      .kind(flecs::PreFrame)
      .each([answers](flecs::iter& it, size_t, NetSession& session, const ServerRole& role,
                      const z13::LanDiscoverySettings& settings) {
        Answer(it.world(), session, *answers, role.port, settings);
      });
  const auto state = std::make_shared<LanDiscoveryState>();
  world.system<const z13::LanDiscoverySettings>("LanDiscoverySystem::Look")
      .kind(flecs::PreFrame)
      .read<LanSearch>()
      .each([state](flecs::iter& it, size_t, const z13::LanDiscoverySettings& settings) {
        Look(it.world(), *state, settings);
      });
}

}  // namespace

void LanDiscoverySystem::Register(flecs::world& world) {
  OnRegisterComponents(world, RegisterComponents);
  OnInitSystems(world, RegisterSystems);
}

}  // namespace z13::net
