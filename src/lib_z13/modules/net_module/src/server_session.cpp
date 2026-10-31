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
#include <expected>
#include <format>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

#include <lib_core/settings/config.h>
#include <lib_core/time/simulation_clock.h>
#include <lib_core/utils/flecs_utils.h>
#include <lib_core/utils/log.h>
#include <lib_profile/profile.h>

#include <z13/components/action_map.h>
#include <z13/components/player_action.h>
#include <z13_module/input/action_negotiation.h>
#include <z13_settings/settings.h>

#include <net_module/clock_sync.h>
#include <net_module/divergence_dump.h>
#include <net_module/protocol.h>

#include "action_hints.h"
#include "catch_up.h"
#include <Eigen/Dense>

#include <sectors/sector.h>
#include <z13/components/block.h>

#include "interest_events.h"
#include "interest_view.h"
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

// Inside the late window, past a snapshot a rollback can start from (scanned: a join pushes its
// snapshot out of tick order), and no further ahead than the schedule window.
ScheduleWindow MakeScheduleWindow(flecs::world world, uint64_t now, const NetTuning& tuning) {
  const uint64_t late_start = now > tuning.max_late_ticks ? now - tuning.max_late_ticks : 0;
  const std::optional<uint64_t> oldest = OldestSnapshotTick(world);
  const uint64_t earliest = oldest ? std::min(now, std::max(late_start, *oldest + 1)) : now;
  return {.earliest = earliest, .latest = now + tuning.max_schedule_ahead_ticks};
}

// Moved to the window's nearest edge rather than dropped (CLAUDE.md). It stays after the action's
// previous command and, unless absolute (the newer value rightly wins), off its tick: one press would hide the other.
ScheduledCommandTick ScheduleTick(
    const ScheduleWindow& window, uint64_t tick, bool absolute, std::optional<ScheduledCommandTick> last) {
  uint64_t scheduled = std::clamp(tick, window.earliest, window.latest);
  if (last && scheduled <= last->tick) {
    const bool apart = !absolute && (scheduled != tick || last->moved || scheduled < last->tick);
    scheduled = std::min(last->tick + (apart ? 1 : 0), window.latest);
  }
  return {.tick = scheduled, .moved = scheduled != tick};
}

Status SendWelcome(
    NetSession& session, flecs::world world, ConnectionId connection, uint32_t player_id,
    std::vector<uint32_t> action_ids, const std::optional<InterestView>& view) {
  Z13_PROFILE_ZONE("SendWelcome");
  auto catch_up = MakeCatchUp(world, view);
  if (!catch_up) {
    return std::unexpected(catch_up.error());
  }
  const uint64_t snapshot_tick = (*catch_up)->snapshot_tick;
  fbn::WelcomeT welcome;
  welcome.player_id = player_id;
  welcome.catch_up = std::move(*catch_up);
  welcome.action_ids = std::move(action_ids);
  const auto config = z13::GetCoreConfig(world);
  const bool dedicated = config && config->get().IsServer();
  welcome.fps = config ? config->get().GetFPS() : z13::CoreSettings {}.fps;
  welcome.tuning = std::make_unique<fbs::net::NetTuningT>(TuningForClients(world.get<NetTuning>(), dedicated));
  welcome.physics = std::make_unique<fbs::physics::PhysicsTuningT>(world.get<z13::PhysicsTuning>());
  welcome.building = std::make_unique<fbs::building::BuildingTuningT>(world.get<z13::BuildingTuning>());

  Envelope envelope;
  envelope.body.Set(std::move(welcome));
  session.Send(connection, Channel::kReliable, envelope);

  // The joiner ignores session deltas received before Welcome.
  const auto& deltas = world.get<ScheduledSessionDeltas>();
  const auto newer = std::ranges::upper_bound(deltas.history, snapshot_tick, {}, &ScheduledSessionDelta::apply_tick);
  SendSessionDeltas(session, connection, player_id, {newer, deltas.history.end()}, view);
  SendSessionDeltas(session, connection, player_id, deltas.pending, view);
  RememberResync(world, player_id, view);
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
    const z13::gameplay::IdCounters& counters) {
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
  if (const auto mismatch = UtilityModulesMismatch(UtilityModulesOf(world), hello.modules)) {
    SendRejected(session, connection, *mismatch);
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

  // last_player_id holds the next id to hand out, as in OnInit; ApplyPlayerJoined raises it.
  auto& allocator = world.get_mut<PlayerIdAllocator>();
  const uint32_t player_id = std::max(counters.last_player_id, allocator.next_player_id);
  allocator.next_player_id = player_id + 1;

  session.BindPlayer(connection, player_id);
  log_info("NetSession(server): connection {} joins as player {}", connection, player_id);
  auto join = PrepareJoin(world, player_id);
  if (!join) {
    return std::unexpected(join.error());
  }
  // The joiner is not in the sector index until it spawns, so its view is the one around where it will.
  std::optional<InterestView> view;
  if (world.get<NetTuning>().interest_events) {
    const int sector_cells = world.get<NetTuning>().sector_size_cells;
    const Eigen::Vector3i cell = (join->spawn_position / z13::station::kCellSize).array().floor().cast<int>();
    view = ViewAround(world, player_id, sectors::SectorOf(cell, sector_cells), join->spawn_position);
  }
  return SendWelcome(session, world, connection, player_id, std::move(*action_ids), view).and_then([&] {
    ScheduleJoin(session, world, player_id, std::move(*join));
    return Status {};
  });
}

Status HandleResyncRequest(
    NetSession& session, flecs::world world, ConnectionId connection, const fbn::ResyncRequestT& request) {
  const std::optional<uint32_t> player_id = session.PlayerIdFor(connection);
  if (!player_id) {
    log_warn("NetSession(server): ignoring a ResyncRequest from an unwelcomed connection {}", connection);
    return {};
  }
  log_info("NetSession(server): resyncing connection {}", connection);
  if (request.diverged_tick) {
    const auto dumped = DumpDivergedTick(world, *request.diverged_tick, std::format("server_for_{}", *player_id));
    if (!dumped) {
      log_warn("NetSession(server): cannot write the diverged tick: {}", dumped.error());
    } else if (*dumped) {
      log_info("NetSession(server): wrote {}", (*dumped)->string());
    }
  }
  // With interest events the Resync is the client's view, and what it holds is what the server then knows.
  const std::optional<InterestView> view =
      world.get<NetTuning>().interest_events ? ViewOf(world, *player_id) : std::nullopt;
  auto catch_up = MakeCatchUp(world, view);
  if (!catch_up) {
    return std::unexpected(catch_up.error());
  }
  RememberResync(world, *player_id, view);
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

// Refills by what an honest client records a tick (a command per action at most) and holds what
// it may bring after a stall (the late and ahead windows), so player input is never dropped.
bool AllowCommand(CommandRateLimits& limits, ConnectionId connection, uint64_t now, const NetTuning& tuning,
                  size_t actions) {
  const uint64_t capacity = actions * (tuning.max_late_ticks + tuning.max_schedule_ahead_ticks);
  const auto [entry, added] =
      limits.by_connection.try_emplace(connection, ConnectionRateLimit {.refilled_tick = now, .commands_left = capacity});
  ConnectionRateLimit& budget = entry->second;
  const uint64_t elapsed = now > budget.refilled_tick ? now - budget.refilled_tick : 0;
  budget.commands_left = std::min(capacity, budget.commands_left + actions * elapsed);
  budget.refilled_tick = now;
  if (budget.commands_left == 0) {
    return false;
  }
  --budget.commands_left;
  return true;
}

void HandleCommandBatch(
    NetSession& session, flecs::world world, ConnectionId connection, const fbn::CommandBatchT& batch) {
  Z13_PROFILE_ZONE("HandleCommandBatch");
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

  auto& last_ticks = world.get_mut<LastCommandTicks>().by_action;
  const ScheduleWindow window = MakeScheduleWindow(world, now, tuning);
  std::vector<z13::gameplay::PlayerActionRecord> accepted;
  fbn::CommandsRetimedT retimed;
  for (const fbs::net::CommandWire& command : batch.commands) {
    if (!IsKnownActionId(action_map, command.action_id())) {
      log_warn(
          "NetSession(server): dropping an unknown action id {} from connection {}", command.action_id(),
          connection);
      continue;
    }
    if (!AllowCommand(rate_limits, connection, now, tuning, action_map.action_map.size())) {
      log_warn("NetSession(server): connection {} exceeded its command rate limit, dropping the rest of this batch",
          connection);
      break;
    }
    z13::gameplay::PlayerActionRecord record = FromWire(command, batch.base_tick, *player_id);
    const PlayerActionKey key {.player_id = *player_id, .action_id = record.action_id};
    const auto last = last_ticks.find(key);
    const ScheduledCommandTick scheduled = ScheduleTick(
        window, record.tick, IsAbsoluteAction(action_map, record.action_id),
        last == last_ticks.end() ? std::nullopt : std::optional(last->second));
    if (scheduled.moved) {
      log_debug("NetSession(server): moved action {} = {} of player {} from tick {} to {} (now {})", record.action_id,
                record.value, *player_id, record.tick, scheduled.tick, now);
      retimed.moves.emplace_back(record.tick, scheduled.tick, command.action_id(), command.value());
      record.tick = scheduled.tick;
    }
    last_ticks.insert_or_assign(key, scheduled);
    QueueInOrder(queue, record);
    accepted.push_back(record);
  }
  if (!retimed.moves.empty()) {
    log_info("NetSession(server): moved {} late or early commands from connection {}", retimed.moves.size(), connection);
    Envelope envelope;
    envelope.body.Set(std::move(retimed));
    session.Send(connection, Channel::kReliable, envelope);
  }
  // Capped like a command's tick, so a client can't claim input far ahead.
  const uint64_t through_tick = std::min(batch.through_tick, now + tuning.max_schedule_ahead_ticks);
  uint64_t& received = world.get_mut<ReceivedInputTicks>().by_player[*player_id];
  received = std::max(received, through_tick);
  const bool confirmed_more = ConfirmInputThrough(world, *player_id, through_tick);
  if (accepted.empty() && !confirmed_more) {
    return;
  }

  auto run = ToCommandRun(accepted, through_tick);
  if (!run) {
    log_error("NetSession(server): can't relay commands from connection {}: {}", connection, run.error());
    return;
  }
  RelayCommands(world, session, *player_id, through_tick, *run, connection);
}

void HandleServerDisconnect(NetSession& session, flecs::world world, ConnectionId connection) {
  const std::optional<uint32_t> player_id = session.PlayerIdFor(connection);
  session.RemoveConnection(connection);
  world.get_mut<CommandRateLimits>().by_connection.erase(connection);
  if (!player_id) {
    return;  // never completed the handshake
  }
  log_info("NetSession(server): player {} left (connection {})", *player_id, connection);
  ForgetClient(world.get_mut<InterestKnowledge>(), *player_id);
  std::erase_if(world.get_mut<LastCommandTicks>().by_action, [&](const auto& entry) {
    return entry.first.player_id == *player_id;
  });

  fbn::PlayerLeftT left;
  left.apply_tick = LeaveApplyTick(world, *player_id);
  left.player_id = *player_id;
  const uint64_t apply_tick = left.apply_tick;
  ScheduleSessionDelta(session, world, apply_tick, std::move(left), *player_id);
}

Status HandleServerReceived(
    NetSession& session, flecs::world world, const TransportEvent& event, const z13::gameplay::IdCounters& counters) {
  const ConnectionId connection = event.connection;
  if (event.channel == Channel::kHints) {
    HandleHintPacket(session, world, connection, AsUint8(event.data));
    return {};
  }
  if (event.channel == Channel::kPoses) {
    return {};  // only the server sends poses
  }
  const auto decoded = DecodeMessage(AsUint8(event.data));
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
      [&](const fbn::ResyncRequestT& request) {
        return HandleResyncRequest(session, world, connection, request);
      },
      [&](const fbn::CommandBatchT& batch) {
        HandleCommandBatch(session, world, connection, batch);
        return Status {};
      },
      [](const auto&) { return Status {}; },
  });
}

}  // namespace

void FinalizeLateInput(NetSession& session, flecs::world world) {
  Z13_PROFILE_ZONE("FinalizeLateInput");
  const uint64_t now = world.get<ft::SimulationClock>().tick;
  const auto& tuning = world.get<NetTuning>();
  const uint64_t earliest = MakeScheduleWindow(world, now, tuning).earliest;
  if (now % tuning.send_interval_ticks != 0 || earliest == 0) {
    return;
  }
  const uint64_t final_tick = earliest - 1;
  std::vector<std::pair<uint32_t, uint64_t>> behind;
  for (const auto& [player_id, through] : world.get<z13::gameplay::ConfirmedInputTicks>().by_player) {
    if (through < final_tick) {
      behind.emplace_back(player_id, through);
    }
  }
  auto& queue = world.get_mut<z13::gameplay::ScheduledCommands>();
  auto& last_ticks = world.get_mut<LastCommandTicks>().by_action;
  for (const auto& [player_id, through] : behind) {
    const std::vector<z13::gameplay::PlayerActionRecord> releases = ReleasesAfter(world, player_id, through);
    for (const z13::gameplay::PlayerActionRecord& release : releases) {
      QueueInOrder(queue, release);
      const PlayerActionKey key {.player_id = player_id, .action_id = release.action_id};
      if (const auto last = last_ticks.find(key); last == last_ticks.end() || last->second.tick < release.tick) {
        last_ticks.insert_or_assign(key, ScheduledCommandTick {.tick = release.tick});
      }
    }
    ConfirmInputThrough(world, player_id, final_tick);
    if (releases.empty()) {
      continue;  // the next digest's input_through carries the confirmation
    }
    auto run = ToCommandRun(releases, final_tick);
    if (!run) {
      log_error("NetSession(server): can't send the releases of player {}: {}", player_id, run.error());
      continue;
    }
    log_info("NetSession(server): player {}'s input stopped at tick {}, releasing {} held actions", player_id,
             through, releases.size());
    RelayCommands(world, session, player_id, final_tick, *run);
  }
}

Status ServiceServerSession(flecs::world world, NetSession& session, const z13::gameplay::IdCounters& counters) {
  Z13_PROFILE_ZONE("ServiceServerSession");
  for (const TransportEvent& event : session.Service()) {
    switch (event.kind) {
      case TransportEventKind::kConnected:
        log_info("NetSession(server): connection {} opened", event.connection);
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
