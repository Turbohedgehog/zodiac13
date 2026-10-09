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

#include "server_session.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

#include <lib_core/state/world_snapshot_history.h>
#include <lib_core/time/simulation_clock.h>
#include <lib_core/utils/flecs_utils.h>
#include <lib_core/utils/log.h>

#include <z13/components/input.h>
#include <z13/components/player_action.h>
#include <z13_module/input/action_negotiation.h>
#include <z13_settings/settings.h>

#include <net_module/protocol.h>

#include "catch_up.h"
#include "remote_input.h"
#include "scheduled_commands.h"
#include "session_deltas.h"
#include "session_settings.h"
#include "wire_records.h"

namespace z13::net {

namespace {

namespace ft = z13::flecs_tools;
namespace fbn = fbs::net;
namespace zgi = z13::gameplay::input;

// Never moved: the sender has already applied its own copy on this tick.
bool IsWithinScheduleWindow(uint64_t apply_tick, uint64_t now, const NetTuning& tuning) {
  return apply_tick + tuning.max_late_ticks >= now && apply_tick <= now + tuning.max_schedule_ahead_ticks;
}

bool CanReplayFrom(flecs::world world, uint64_t tick) {
  const auto& entries = world.get<ft::WorldSnapshotHistory>().history.Entries();
  return std::ranges::any_of(entries, [tick](const ft::TimestampedSnapshot& entry) { return entry.tick < tick; });
}

Status SendWelcome(
    NetSession& session, flecs::world world, ConnectionId connection, uint32_t player_id,
    std::vector<uint32_t> action_ids) {
  auto catch_up = MakeCatchUp(world);
  if (!catch_up) {
    return std::unexpected(catch_up.error());
  }
  const uint64_t snapshot_tick = (*catch_up)->snapshot_tick;
  fbn::WelcomeT welcome;
  welcome.player_id = player_id;
  welcome.catch_up = std::move(*catch_up);
  welcome.action_ids = std::move(action_ids);
  const auto config = z13::GetCoreConfig(world);
  welcome.fps = config ? config->get().GetFPS() : z13::CoreSettings {}.fps;
  welcome.tuning = std::make_unique<fbs::net::NetTuningT>(world.get<NetTuning>());
  welcome.physics = std::make_unique<fbs::physics::PhysicsTuningT>(world.get<z13::PhysicsTuning>());
  welcome.building = std::make_unique<fbs::building::BuildingTuningT>(world.get<z13::BuildingTuning>());

  Envelope envelope;
  envelope.body.Set(std::move(welcome));
  session.Send(connection, Channel::kReliable, envelope);

  // The joiner ignores session deltas received before Welcome.
  const auto& deltas = world.get<ScheduledSessionDeltas>();
  const auto newer = std::ranges::upper_bound(deltas.history, snapshot_tick, {}, &ScheduledSessionDelta::apply_tick);
  SendSessionDeltas(session, connection, {newer, deltas.history.end()});
  SendSessionDeltas(session, connection, deltas.pending);
  return {};
}

void SendRejected(NetSession& session, ConnectionId connection, std::string reason) {
  fbn::RejectedT rejected;
  rejected.reason = std::move(reason);

  Envelope envelope;
  envelope.body.Set(std::move(rejected));
  session.Send(connection, Channel::kReliable, envelope);
}

std::expected<std::vector<uint32_t>, std::string> NegotiateActions(flecs::world world, const fbn::ClientHelloT& hello) {
  std::vector<zgi::ActionDescriptor> descriptors;
  descriptors.reserve(hello.actions.size());
  for (const auto& action : hello.actions) {
    if (!action) {
      return std::unexpected(std::string {"malformed action list"});
    }
    descriptors.push_back({.enum_name = action->enum_name, .value_name = action->value_name, .enum_value = action->enum_value});
  }
  return zgi::RegisterRemoteActions(world.get_mut<z13::input::ActionMap>(), descriptors);
}

Status HandleClientHello(
    NetSession& session, flecs::world world, ConnectionId connection, const fbn::ClientHelloT& hello,
    z13::gameplay::IdCounters& counters) {
  if (hello.version != kProtocolVersion) {
    SendRejected(session, connection, "protocol version mismatch");
    return {};
  }
  const std::optional<uint64_t> client_palette =
      hello.palette_hash ? std::optional<uint64_t>(*hello.palette_hash) : std::nullopt;
  if (client_palette != PaletteHash(world)) {
    SendRejected(session, connection, "block palette mismatch");
    return {};
  }
  if (session.PlayerIdFor(connection)) {
    // Honouring a second hello would spawn another player and orphan the first.
    log_warn("NetSession(server): ignoring a repeated ClientHello on connection {}", connection);
    return {};
  }

  // Before an id is spent: a refused client must not cost one.
  auto action_ids = NegotiateActions(world, hello);
  if (!action_ids) {
    log_warn("NetSession(server): refusing connection {}: {}", connection, action_ids.error());
    SendRejected(session, connection, std::move(action_ids.error()));
    return {};
  }

  // last_player_id holds the next id to hand out, as in OnInit.
  auto& allocator = world.get_mut<PlayerIdAllocator>();
  const uint32_t player_id = std::max(counters.last_player_id, allocator.next_player_id);
  allocator.next_player_id = counters.last_player_id = player_id + 1;

  session.BindPlayer(connection, player_id);
  return SendWelcome(session, world, connection, player_id, std::move(*action_ids)).and_then([&] {
    return SchedulePlayerJoined(session, world, player_id);
  });
}

Status HandleResyncRequest(NetSession& session, flecs::world world, ConnectionId connection) {
  if (!session.PlayerIdFor(connection)) {
    log_warn("NetSession(server): ignoring a ResyncRequest from an unwelcomed connection {}", connection);
    return {};
  }
  log_info("NetSession(server): resyncing connection {}", connection);
  auto catch_up = MakeCatchUp(world);
  if (!catch_up) {
    return std::unexpected(catch_up.error());
  }
  fbn::ResyncT resync;
  resync.catch_up = std::move(*catch_up);

  Envelope envelope;
  envelope.body.Set(std::move(resync));
  session.Send(connection, Channel::kReliable, envelope);
  return {};
}

void HandlePing(NetSession& session, flecs::world world, ConnectionId connection, const fbn::PingT& ping) {
  fbn::PongT pong;
  pong.client_tick = ping.client_tick;
  pong.server_tick = world.get<ft::SimulationClock>().tick;

  Envelope envelope;
  envelope.body.Set(std::move(pong));
  session.Send(connection, Channel::kUnreliable, envelope);
}

bool AllowCommand(CommandRateLimits& limits, ConnectionId connection, uint64_t now, const NetTuning& tuning) {
  ConnectionRateLimit& state = limits.by_connection[connection];
  if (now - state.window_start_tick >= tuning.command_rate_limit_window_ticks) {
    state.window_start_tick = now;
    state.commands_this_window = 0;
  }
  if (state.commands_this_window >= tuning.max_commands_per_rate_limit_window) {
    return false;
  }
  ++state.commands_this_window;
  return true;
}

void HandleCommandBatch(
    NetSession& session, flecs::world world, ConnectionId connection, const fbn::CommandBatchT& batch) {
  const std::optional<uint32_t> player_id = session.PlayerIdFor(connection);
  if (!player_id) {
    log_warn("NetSession(server): dropping a CommandBatch from an unwelcomed connection {}", connection);
    return;
  }

  const uint64_t now = world.get<ft::SimulationClock>().tick;
  auto& queue = world.get_mut<z13::gameplay::ScheduledCommands>();
  const auto& action_map = world.get<z13::input::ActionMap>();
  auto& rate_limits = world.get_mut<CommandRateLimits>();
  const auto& tuning = world.get<NetTuning>();

  std::vector<fbs::net::CommandWire> accepted;
  for (const fbs::net::CommandWire& command : batch.commands) {
    if (!IsKnownActionId(action_map, command.action_id())) {
      log_warn(
          "NetSession(server): dropping an unknown action id {} from connection {}", command.action_id(),
          connection);
      continue;
    }
    const z13::gameplay::PlayerActionRecord record = FromWire(command, batch.base_tick, *player_id);
    if (!IsWithinScheduleWindow(record.tick, now, tuning) || (record.tick < now && !CanReplayFrom(world, record.tick))) {
      log_warn("NetSession(server): dropping a command for tick {} from connection {} at tick {}", record.tick,
          connection, now);
      continue;
    }
    if (!AllowCommand(rate_limits, connection, now, tuning)) {
      log_warn("NetSession(server): connection {} exceeded its command rate limit, dropping the rest of this batch",
          connection);
      break;
    }
    QueueInOrder(queue, record);
    accepted.push_back(command);
  }
  // Capped like a command's tick, so a client can't claim input far ahead.
  const uint64_t through_tick = std::min(batch.through_tick, now + tuning.max_schedule_ahead_ticks);
  const bool confirmed_more = ConfirmInputThrough(world, *player_id, through_tick);
  if (accepted.empty() && !confirmed_more) {
    return;
  }

  fbn::SequencedCommandsT sequenced;
  sequenced.player_id = *player_id;
  sequenced.base_tick = batch.base_tick;
  sequenced.commands = std::move(accepted);
  sequenced.through_tick = through_tick;

  Envelope envelope;
  envelope.body.Set(std::move(sequenced));
  session.Broadcast(Channel::kReliable, envelope, connection);
}

void HandleServerDisconnect(NetSession& session, flecs::world world, ConnectionId connection) {
  const std::optional<uint32_t> player_id = session.PlayerIdFor(connection);
  session.RemoveConnection(connection);
  world.get_mut<CommandRateLimits>().by_connection.erase(connection);
  if (!player_id) {
    return;  // never completed the handshake
  }

  fbn::PlayerLeftT left;
  left.apply_tick = LeaveApplyTick(world, *player_id);
  left.player_id = *player_id;
  const uint64_t apply_tick = left.apply_tick;
  ScheduleSessionDelta(session, world, apply_tick, std::move(left));
}

Status HandleServerReceived(
    NetSession& session, flecs::world world, const TransportEvent& event, z13::gameplay::IdCounters& counters) {
  const auto decoded = DecodeMessage(AsUint8(event.data));
  const ConnectionId connection = event.connection;
  if (!decoded) {
    log_warn("NetSession(server): dropping malformed packet from connection {}: {}", connection, decoded.error());
    // Disconnect() raises no kDisconnected for us.
    session.Disconnect(connection);
    HandleServerDisconnect(session, world, connection);
    return {};
  }

  return VisitBody(decoded->body, Overloaded {
      [&](const fbn::ClientHelloT& hello) { return HandleClientHello(session, world, connection, hello, counters); },
      [&](const fbn::PingT& ping) {
        HandlePing(session, world, connection, ping);
        return Status {};
      },
      [&](const fbn::ResyncRequestT&) { return HandleResyncRequest(session, world, connection); },
      [&](const fbn::CommandBatchT& batch) {
        HandleCommandBatch(session, world, connection, batch);
        return Status {};
      },
      [](const auto&) { return Status {}; },
  });
}

}  // namespace

Status ServiceServerSession(flecs::world world, NetSession& session, z13::gameplay::IdCounters& counters) {
  for (const TransportEvent& event : session.Service()) {
    switch (event.kind) {
      case TransportEventKind::kConnected:
        session.AddConnection(event.connection);
        break;
      case TransportEventKind::kDisconnected:
        HandleServerDisconnect(session, world, event.connection);
        break;
      case TransportEventKind::kReceived:
        if (const Status handled = HandleServerReceived(session, world, event, counters); !handled) {
          return handled;
        }
        break;
    }
  }
  return {};
}

}  // namespace z13::net
