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
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <lib_core/state/rollback.h>
#include <lib_core/state/world_snapshot_history.h>
#include <lib_core/time/simulation_clock.h>
#include <lib_core/utils/log.h>
#include <lib_profile/profile.h>

#include <z13/components/gameplay.h>
#include <z13/components/player.h>
#include <z13/components/action_map.h>
#include <z13/components/net.h>
#include <z13/components/player_action.h>
#include <z13_module/gameplay/gameplay_entities.h>
#include <z13_module/input/action_negotiation.h>
#include <z13_settings/net_tuning.h>

#include <net_module/clock_sync.h>
#include <net_module/divergence_dump.h>
#include <net_module/protocol.h>
#include <net_module/state_digest.h>

#include "action_hints.h"
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

template <typename T>
std::string OptionalText(const std::optional<T>& value) {
  return value ? std::to_string(*value) : std::string("none");
}

// The Welcome already put the client in a game; one that failed has no scene, so back to the menu.
void FailJoin(flecs::world world, std::string reason) {
  world.remove<z13::gameplay::Gameplay>();
  world.add<z13::gameplay::Pause>();
  FailSession(world, std::move(reason));
}

Status JoinFromWelcome(flecs::world world, const fbn::WelcomeT& welcome) {
  Z13_PROFILE_ZONE("JoinFromWelcome");
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
  // A joining client is frozen until here; the replay to the server starts this frame.
  world.remove<ft::SimulationFrozen>();
  world.set<ClockSync>({});

  // ConnectionStatus stays kConnecting until the catch-up rollback lands.
  const uint64_t target_tick = payload->server_tick;
  AdoptCatchUp(world, std::move(*payload), target_tick);
  return {};
}

void HandleWelcome(flecs::world world, const fbn::WelcomeT& welcome) {
  if (const Status joined = JoinFromWelcome(world, welcome); !joined) {
    log_warn("NetSession(client): unusable Welcome: {}", joined.error());
    FailJoin(world, joined.error());
  } else {
    log_info("NetSession(client): joined as player {}", welcome.player_id);
  }
}

// Unlike a join, never moves this clock backwards.
void HandleResync(flecs::world world, const fbn::ResyncT& resync) {
  Z13_PROFILE_ZONE("HandleResync");
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
  // Only FinalizeLateInput sends a player its own records: releases of what it held while its input
  // didn't arrive. A key still down is then recorded as pressed again.
  const bool own = sequenced.player_id == world.get<z13::gameplay::LocalPlayer>().id;
  auto& last_recorded = world.get_mut<z13::gameplay::LastRecordedActionValues>().values;
  for (const fbs::net::CommandWire& command : sequenced.commands) {
    const z13::gameplay::PlayerActionRecord record = FromWire(command, sequenced.base_tick, sequenced.player_id);
    QueueInOrder(queue, record);
    if (own) {
      last_recorded.insert_or_assign(record.action_id, record.value);
    }
  }
  ConfirmInputThrough(world, sequenced.player_id, sequenced.through_tick);
}

void MoveOwnCommands(flecs::world world, const fbn::CommandsRetimedT& retimed) {
  const uint32_t local_id = *world.get<z13::gameplay::LocalPlayer>().id;
  const uint64_t now = world.get<ft::SimulationClock>().tick;
  auto& log = world.get_mut<z13::gameplay::PlayerActionLog>().log;
  auto& queue = world.get_mut<z13::gameplay::ScheduledCommands>();
  std::optional<uint64_t> earliest;
  for (const fbn::CommandMove& move : retimed.moves) {
    const z13::gameplay::PlayerActionRecord moved {
        .tick = move.to_tick(),
        .player_id = local_id,
        .action_id = move.action_id(),
        .value = z13::gameplay::DequantizeActionValue(move.value()),
    };
    log_debug("NetSession(client): the server moved own action {} = {} from tick {} to {} (now {})",
              moved.action_id, moved.value, move.from_tick(), moved.tick, now);
    // One record, matched by value too: an earlier move may have put another of this action on that tick.
    bool removed = false;
    log.RemoveIf([&](const z13::gameplay::PlayerActionRecord& record) {
      const bool match = !removed && record.tick == move.from_tick() && record.player_id == local_id &&
                         record.action_id == moved.action_id && record.value == moved.value;
      removed = removed || match;
      return match;
    });
    if (!removed) {
      log_warn("NetSession(client): no own action {} = {} at tick {} to move", moved.action_id, moved.value,
               move.from_tick());
    }
    if (moved.tick > now) {
      QueueInOrder(queue, moved);
    } else {
      log.MergeSorted({moved}, RecordLess);
    }
    earliest = std::min({earliest.value_or(moved.tick), move.from_tick(), moved.tick});
  }
  if (earliest && *earliest < now && *earliest > 0) {
    // A record moved from before the oldest snapshot (a catch-up's base, sent while awaiting it) is
    // already absent from that snapshot, the server's state: replaying from it is enough (Н4).
    log_debug("NetSession(client): moved own commands from tick {} ask a rollback (now {})", *earliest, now);
    ft::DeferRollback(world, std::max(*earliest - 1, OldestSnapshotTick(world).value_or(*earliest - 1)));
  }
}

void HandleClientReceived(flecs::world world, const TransportEvent& event) {
  if (event.channel == Channel::kHints) {
    if (world.get<z13::gameplay::LocalPlayer>().id) {
      StoreRelayedHints(world, AsUint8(event.data));
    }
    return;
  }
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
        auto& sync = world.get_mut<ClockSync>();
        const uint64_t received_tick = world.get<ft::SimulationClock>().tick;
        const bool answers_ping = sync.ping_sent_tick == pong.client_tick && received_tick >= pong.client_tick;
        const std::string offset_before = OptionalText(sync.offset_ticks);
        ApplyPong(sync, world.get<NetTuning>(), pong.client_tick, pong.server_tick, received_tick,
                  world.get<ft::ClockAdjustRequest>());
        if (answers_ping) {
          log_debug("NetSession(client): pong for tick {} at tick {}: server tick {}, rtt {}, offset {} -> {}",
                    pong.client_tick, received_tick, pong.server_tick, OptionalText(sync.rtt_ticks), offset_before,
                    OptionalText(sync.offset_ticks));
        } else {
          log_debug("NetSession(client): ignored a pong for tick {} at tick {} (server tick {}, ping in flight: {})",
                    pong.client_tick, received_tick, pong.server_tick, OptionalText(sync.ping_sent_tick));
        }
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
      [world, welcomed](const fbn::CommandsRetimedT& retimed) {
        if (welcomed) {
          MoveOwnCommands(world, retimed);
        }
      },
      [world, welcomed](const fbn::StateDigestT& digest) {
        auto& digests = world.get_mut<StateDigests>();
        if (!welcomed || digests.awaiting_resync) {
          return;
        }
        // The server's final confirmations: the digest is compared once their rollbacks land.
        const uint32_t local_id = *world.get<z13::gameplay::LocalPlayer>().id;
        for (const fbn::PlayerTickWire& through : digest.input_through) {
          if (through.player_id() != local_id) {
            ConfirmInputThrough(world, through.player_id(), through.tick());
          }
        }
        digests.received.push_back(digest);
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
        log_info("NetSession(client): connected, saying hello");
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
  fbn::ResyncRequestT request;
  if (const std::optional<DigestMismatch> mismatch = CheckReceivedStateDigests(world, digests)) {
    log_warn("NetSession(client): {}", mismatch->description);
    diverged = true;
    request.diverged_tick = mismatch->tick;
    // Checked only once joined, so the local player has an id.
    const uint32_t local_id = world.get<z13::gameplay::LocalPlayer>().id.value();
    const auto dumped = DumpDivergedTick(world, mismatch->tick, std::format("client_{}", local_id));
    if (!dumped) {
      log_warn("NetSession(client): cannot write the diverged tick: {}", dumped.error());
    } else if (*dumped) {
      log_info("NetSession(client): wrote {}", (*dumped)->string());
    }
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
  envelope.body.Set(std::move(request));
  session.Send(*server_connection, Channel::kReliable, envelope);
}

bool FinishPendingJoin(flecs::world world) {
  if (world.has<ft::RollbackFailed>()) {
    const std::string reason = world.get<ft::RollbackFailed>().reason;
    world.remove<ft::RollbackFailed>();
    FailJoin(world, reason);
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
  sync.applied_at_ping = world.get<ft::ClockAdjustRequest>().applied;
}

}  // namespace z13::net
