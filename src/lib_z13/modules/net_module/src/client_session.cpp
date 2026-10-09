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

#include "client_session.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <lib_core/state/rollback.h>
#include <lib_core/state/world_snapshot_history.h>
#include <lib_core/time/simulation_clock.h>
#include <lib_core/utils/log.h>

#include <z13/components/gameplay.h>
#include <z13/components/input.h>
#include <z13/components/net.h>
#include <z13/components/player_action.h>
#include <z13_module/gameplay/gameplay_entities.h>
#include <z13_module/input/action_negotiation.h>
#include <z13_settings/net_tuning.h>

#include <net_module/clock_sync.h>
#include <net_module/protocol.h>
#include <net_module/state_digest.h>

#include "catch_up.h"
#include "remote_input.h"
#include "scheduled_commands.h"
#include "session_control.h"
#include "session_deltas.h"
#include "session_settings.h"
#include "state_digest_system.h"
#include "wire_records.h"

namespace z13::net {

namespace {

namespace ft = z13::flecs_tools;
namespace fbn = fbs::net;
namespace zgi = z13::gameplay::input;

Status JoinFromWelcome(flecs::world world, const fbn::WelcomeT& welcome) {
  auto payload = DecodeCatchUp(welcome.catch_up);
  if (!payload) {
    return std::unexpected("corrupt snapshot");
  }
  const auto session_settings = DecodeSessionSettings(world, welcome);
  if (!session_settings) {
    return std::unexpected(session_settings.error());
  }
  // Before the plausibility check, which is bounded by the server's catch-up limit.
  AdoptSessionSettings(world, *session_settings);
  world.set<ft::SnapshotCaptureRate>({.per_interval = world.get<NetTuning>().rollback_snapshots_per_interval});

  if (!IsPlausibleCatchUp(world, payload->snapshot_tick, payload->server_tick)) {
    return std::unexpected("implausible server tick");
  }
  if (const auto adopted = zgi::AdoptActionIds(world, welcome.action_ids); !adopted) {
    return adopted;
  }

  world.set<z13::gameplay::LocalPlayer>({.id = welcome.player_id});
  world.remove<z13::gameplay::Pause>();
  world.add<z13::gameplay::Gameplay>();
  world.set<ClockSync>({});

  // ConnectionStatus stays kConnecting until the catch-up rollback lands.
  const uint64_t target_tick = payload->server_tick;
  AdoptCatchUp(world, std::move(*payload), target_tick);
  return {};
}

void HandleWelcome(flecs::world world, const fbn::WelcomeT& welcome) {
  if (const Status joined = JoinFromWelcome(world, welcome); !joined) {
    log_warn("NetSession(client): unusable Welcome: {}", joined.error());
    FailSession(world, joined.error());
  }
}

// Unlike a join, never moves this clock backwards.
void HandleResync(flecs::world world, const fbn::ResyncT& resync) {
  auto payload = DecodeCatchUp(resync.catch_up);
  const uint64_t target_tick = std::max(payload ? payload->server_tick : 0, world.get<ft::SimulationClock>().tick);
  if (!payload || !IsPlausibleCatchUp(world, payload->snapshot_tick, target_tick)) {
    log_warn("NetSession(client): unusable Resync: {}", payload ? "implausible server tick" : payload.error());
    world.get_mut<StateDigests>().awaiting_resync.reset();  // the next mismatch asks again
    return;
  }
  log_info("NetSession(client): resyncing from tick {}", payload->snapshot_tick);
  AdoptCatchUp(world, std::move(*payload), target_tick);
  ++world.get_mut<StateDigests>().resyncs;
}

void QueueSequencedCommands(flecs::world world, const fbn::SequencedCommandsT& sequenced) {
  auto& queue = world.get_mut<z13::gameplay::ScheduledCommands>();
  for (const fbs::net::CommandWire& command : sequenced.commands) {
    QueueInOrder(queue, FromWire(command, sequenced.base_tick, sequenced.player_id));
  }
  ConfirmInputThrough(world, sequenced.player_id, sequenced.through_tick);
}

void HandleClientReceived(flecs::world world, const TransportEvent& event) {
  const auto decoded = DecodeMessage(AsUint8(event.data));
  if (!decoded) {
    log_warn("NetSession(client): dropping malformed packet: {}", decoded.error());
    return;
  }

  // Commands and session deltas only mean something once Welcome has set the baseline.
  const bool welcomed = world.get<z13::gameplay::LocalPlayer>().id.has_value();
  const auto queue_session_delta = [world, welcomed](const auto& delta) {
    if (welcomed) {
      world.get_mut<ScheduledSessionDeltas>().pending.push_back({.apply_tick = delta.apply_tick, .delta = delta});
    }
  };
  VisitBody(decoded->body, Overloaded {
      [&](const fbn::WelcomeT& welcome) { HandleWelcome(world, welcome); },
      [&](const fbn::RejectedT& rejected) { FailSession(world, rejected.reason); },
      [world](const fbn::PongT& pong) {
        ApplyPong(
            world.get_mut<ClockSync>(), world.get<NetTuning>(), pong.client_tick, pong.server_tick,
            world.get<ft::SimulationClock>().tick);
      },
      [world, welcomed](const fbn::SequencedCommandsT& sequenced) {
        if (welcomed) {
          QueueSequencedCommands(world, sequenced);
        }
      },
      [&](const fbn::PlayerJoinedT& joined) { queue_session_delta(joined); },
      [&](const fbn::PlayerLeftT& left) { queue_session_delta(left); },
      [world, welcomed](const fbn::ResyncT& resync) {
        if (welcomed) {
          HandleResync(world, resync);
        }
      },
      [world, welcomed](const fbn::StateDigestT& digest) {
        auto& digests = world.get_mut<StateDigests>();
        if (welcomed && !digests.awaiting_resync) {
          digests.received.push_back(digest);
        }
      },
      [](const auto&) {},
  });
}

void HandleClientDisconnect(flecs::world world) {
  const bool was_connected = world.has<z13::gameplay::Gameplay>();
  EndSession(world);

  if (was_connected) {
    world.remove<z13::gameplay::Gameplay>();
    world.add<z13::gameplay::Pause>();
    SetConnectionStatus(world, ConnectionState::kDisconnected, "server closed the connection");
  } else {
    SetConnectionStatus(world, ConnectionState::kFailed, "disconnected before joining");
  }
}

void SendClientHello(flecs::world world, NetSession& session, ConnectionId server_connection) {
  fbn::ClientHelloT hello;
  hello.version = kProtocolVersion;
  if (const auto palette_hash = PaletteHash(world)) {
    hello.palette_hash = *palette_hash;
  }
  for (const zgi::ActionDescriptor& action : zgi::DescribeActions(world.get<z13::input::ActionMap>())) {
    auto described = std::make_unique<fbn::ActionDescT>();
    described->enum_name = action.enum_name;
    described->value_name = action.value_name;
    described->enum_value = action.enum_value;
    hello.actions.push_back(std::move(described));
  }

  Envelope envelope;
  envelope.body.Set(std::move(hello));
  session.Send(server_connection, Channel::kReliable, envelope);
}

}  // namespace

void ServiceClientSession(flecs::world world, NetSession& session) {
  for (const TransportEvent& event : session.Service()) {
    switch (event.kind) {
      case TransportEventKind::kConnected:
        session.AddConnection(event.connection);
        session.SetServerConnection(event.connection);
        SendClientHello(world, session, event.connection);
        break;
      case TransportEventKind::kDisconnected:
        HandleClientDisconnect(world);
        return;  // session/NetSession are gone; nothing left in `event`s applies
      case TransportEventKind::kReceived:
        HandleClientReceived(world, event);
        if (!world.has<NetSession>()) {
          return;  // a rejected/corrupt Welcome closed the session; `session` is gone
        }
        break;
    }
  }
}

void ResyncIfDiverged(flecs::world world, NetSession& session) {
  const std::optional<ConnectionId> server_connection = session.ServerConnection();
  if (!IsJoined(world) || !server_connection) {
    return;
  }
  auto& digests = world.get_mut<StateDigests>();
  bool diverged = false;
  if (const std::optional<std::string> mismatch = CheckReceivedStateDigests(world, digests)) {
    log_warn("NetSession(client): {}", *mismatch);
    diverged = true;
  }
  if (world.has<ft::RollbackFailed>()) {
    log_warn("NetSession(client): {}", world.get<ft::RollbackFailed>().reason);
    world.remove<ft::RollbackFailed>();
    diverged = true;
  }
  if (!diverged || digests.awaiting_resync) {
    return;
  }

  digests.awaiting_resync.emplace();
  digests.received.clear();
  Envelope envelope;
  envelope.body.Set(fbn::ResyncRequestT {});
  session.Send(*server_connection, Channel::kReliable, envelope);
}

bool FinishPendingJoin(flecs::world world) {
  if (world.has<ft::RollbackFailed>()) {
    const std::string reason = world.get<ft::RollbackFailed>().reason;
    world.remove<ft::RollbackFailed>();
    FailSession(world, reason);
    return false;
  }

  // The local player's entity arrives as a PlayerJoined delta: its presence says the join landed.
  if (!world.has<ConnectionStatus>() ||
      world.get<ConnectionStatus>().state != ConnectionState::kConnecting) {
    return true;
  }
  const std::optional<uint32_t> local_id = world.get<z13::gameplay::LocalPlayer>().id;
  if (local_id && world.lookup(z13::gameplay::PlayerEntityName(*local_id).c_str())) {
    z13::gameplay::EnsureLocalPlayerReady(world);
    SetConnectionStatus(world, ConnectionState::kConnected);
  }
  return true;
}

void SendPingIfDue(flecs::world world, NetSession& session) {
  if (!IsJoined(world)) {
    return;
  }
  const std::optional<ConnectionId> server_connection = session.ServerConnection();
  const std::optional<uint64_t> ticks_per_second = ft::TicksPerSecond(world);
  if (!server_connection || !ticks_per_second || *ticks_per_second == 0) {
    return;
  }
  const uint64_t tick = world.get<ft::SimulationClock>().tick;
  if (tick % *ticks_per_second != 0) {
    return;
  }

  fbn::PingT ping;
  ping.client_tick = tick;
  Envelope envelope;
  envelope.body.Set(std::move(ping));
  session.Send(*server_connection, Channel::kUnreliable, envelope);
  auto& sync = world.get_mut<ClockSync>();
  sync.ping_sent_tick = tick;
  sync.adjusted_ticks_in_flight = 0;
}

}  // namespace z13::net
