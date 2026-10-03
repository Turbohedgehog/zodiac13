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

#include "net_session_system.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <format>
#include <iterator>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include <boost/container/flat_map.hpp>
#include <flecs.h>

#include <lib_core/components.h>
#include <lib_core/flecs_utils.h>
#include <lib_core/lifecycle.h>
#include <lib_core/log.h>
#include <z13_settings/settings.h>
#include <lib_core/rollback.h>
#include <lib_core/simulation_clock.h>
#include <lib_core/world_serializer.h>
#include <lib_core/world_snapshot_history.h>
#include <lib_core/world_state.h>

#include <z13/components/gameplay.h>
#include <z13/components/input.h>
#include <z13/components/net.h>
#include <z13/components/player_action.h>
#include <z13_module/gameplay/gameplay_entities.h>
#include <z13_module/input/action_negotiation.h>

#include <net_module/clock_sync.h>
#include <net_module/protocol.h>
#include <net_module/state_digest.h>

#include "net_session.h"
#include "scheduled_commands.h"
#include "state_digest_system.h"
#include "transport_factories.h"

namespace z13::net {

namespace {

namespace ft = z13::flecs_tools;
namespace fbn = fbs::net;
namespace zgi = z13::gameplay::input;

std::vector<fbn::ActionRecordWire> ToWire(const std::vector<z13::gameplay::PlayerActionRecord>& records) {
  std::vector<fbn::ActionRecordWire> wire;
  wire.reserve(records.size());
  for (const z13::gameplay::PlayerActionRecord& record : records) {
    wire.emplace_back(record.tick, record.player_id, static_cast<uint32_t>(record.action_id), record.value);
  }
  return wire;
}

std::vector<z13::gameplay::PlayerActionRecord> FromWire(const std::vector<fbn::ActionRecordWire>& wire) {
  std::vector<z13::gameplay::PlayerActionRecord> records;
  records.reserve(wire.size());
  for (const fbn::ActionRecordWire& record : wire) {
    records.push_back({
        .tick = record.tick(),
        .player_id = record.player_id(),
        .action_id = record.action_id(),
        .value = record.value(),
    });
  }
  return records;
}

std::unique_ptr<fbs::state::WorldSnapshotT> ToWire(const ft::WorldSnapshot& snapshot) {
  return std::make_unique<fbs::state::WorldSnapshotT>(ft::ToFlatbuffer(snapshot));
}

// TransportEvent::data is std::byte; the codec layer wants uint8_t, and std::as_bytes
// only converts the other way.
std::span<const uint8_t> AsUint8(std::span<const std::byte> data) {
  return {reinterpret_cast<const uint8_t*>(data.data()), data.size()};
}

// Never moved: the sender has already applied its own copy on this tick.
bool IsWithinScheduleWindow(uint64_t apply_tick, uint64_t now, const NetTuning& tuning) {
  return apply_tick + tuning.max_late_ticks >= now && apply_tick <= now + tuning.max_schedule_ahead_ticks;
}

bool CanReplayFrom(flecs::world world, uint64_t tick) {
  const auto& entries = world.get<ft::WorldSnapshotHistory>().history.Entries();
  return std::ranges::any_of(entries, [tick](const ft::TimestampedSnapshot& entry) { return entry.tick < tick; });
}

// ---------- Server ----------

// PlayerActionLog is sorted by tick.
std::vector<z13::gameplay::PlayerActionRecord> ActionsSince(flecs::world world, uint64_t since_tick) {
  const auto& entries = world.get<z13::gameplay::PlayerActionLog>().log.Entries();
  const auto first = std::ranges::upper_bound(entries, since_tick, {}, &z13::gameplay::PlayerActionRecord::tick);
  return {first, entries.end()};
}

// As of snapshot_tick, not now: the joiner replays `actions` from there.
std::vector<z13::gameplay::PlayerActionRecord> HeldValues(flecs::world world, uint64_t snapshot_tick) {
  const auto& entries = world.get<z13::gameplay::PlayerActionLog>().log.Entries();
  const auto up_to_snapshot = std::ranges::subrange(
      entries.begin(), std::ranges::upper_bound(entries, snapshot_tick, {}, &z13::gameplay::PlayerActionRecord::tick));
  boost::container::flat_map<std::pair<uint32_t, z13::input::ActionInfo::IdType>, float> values;
  std::ranges::for_each(up_to_snapshot, [&values](const z13::gameplay::PlayerActionRecord& record) {
    values[{record.player_id, record.action_id}] = record.value;
  });

  std::vector<z13::gameplay::PlayerActionRecord> held;
  std::ranges::copy(
      values | std::views::filter([](const auto& entry) { return entry.second != 0.f; }) |
          std::views::transform([snapshot_tick](const auto& entry) {
            const auto& [key, value] = entry;
            return z13::gameplay::PlayerActionRecord {
                .tick = snapshot_tick, .player_id = key.first, .action_id = key.second, .value = value};
          }),
      std::back_inserter(held));
  return held;
}

// Stops at the first failed send.
NetSession::Result SendSessionDeltas(
    NetSession& session, ConnectionId connection, std::span<const ScheduledSessionDelta> deltas) {
  NetSession::Result sent;
  const bool all_sent = std::ranges::all_of(deltas, [&](const ScheduledSessionDelta& item) {
    Envelope envelope;
    std::visit([&envelope](auto body) { envelope.body.Set(std::move(body)); }, item.delta);
    sent = session.Send(connection, Channel::kReliable, envelope);
    return sent.has_value();
  });
  return all_sent ? NetSession::Result {} : sent;
}

// The newest snapshot older than any command the server may still accept or has yet to
// replay, so the client can roll back for one; else the oldest, which the server can't go
// past either.
const ft::TimestampedSnapshot& CatchUpBase(
    const ft::WorldSnapshotHistory& history, uint64_t now, std::optional<uint64_t> deferred_rollback_tick,
    uint64_t max_late_ticks) {
  const auto& entries = history.history.Entries();
  const auto settled = std::ranges::find_last_if(entries, [&](const ft::TimestampedSnapshot& entry) {
    return entry.tick + max_late_ticks < now && entry.tick <= deferred_rollback_tick.value_or(entry.tick);
  });
  return settled.empty() ? entries.front() : settled.front();
}

// Welcome and Resync share this catch-up payload.
template <typename Message>
NetSession::Result FillCatchUp(flecs::world world, Message& message) {
  message.server_tick = world.get<ft::SimulationClock>().tick;

  // Reuses WorldSnapshotHistory's cache instead of a fresh CaptureState() per join (too
  // expensive); the action batch lets the client catch up to server_tick by replaying it.
  const auto& history = world.get<ft::WorldSnapshotHistory>();
  if (history.history.Empty()) {
    // Nothing cached yet -- fall back to a fresh capture; nothing to replay either.
    const auto snapshot = ft::CaptureState(world);
    if (!snapshot) {
      return std::unexpected(snapshot.error());
    }
    message.snapshot = ToWire(*snapshot);
    message.snapshot_tick = message.server_tick;
  } else {
    const ft::TimestampedSnapshot& base = CatchUpBase(
        history, message.server_tick, ft::DeferredRollbackTick(world), world.get<NetTuning>().max_late_ticks);
    message.snapshot = ToWire(base.snapshot);
    message.snapshot_tick = base.tick;
    message.actions = ToWire(ActionsSince(world, base.tick));
  }
  message.held_values = ToWire(HeldValues(world, message.snapshot_tick));
  message.pending = ToWire(world.get<z13::gameplay::ScheduledCommands>().records);
  return {};
}

NetSession::Result SendWelcome(
    NetSession& session, flecs::world world, ConnectionId connection, uint32_t player_id,
    std::vector<uint32_t> action_ids) {
  fbn::WelcomeT welcome;
  welcome.player_id = player_id;
  welcome.action_ids = std::move(action_ids);
  const auto config = z13::GetCoreConfig(world);
  welcome.fps = config ? config->get().GetFPS() : z13::CoreSettings {}.fps;
  welcome.tuning = std::make_unique<fbs::net::NetTuningT>(world.get<NetTuning>());
  welcome.physics = std::make_unique<fbs::physics::PhysicsTuningT>(world.get<z13::PhysicsTuning>());
  if (auto filled = FillCatchUp(world, welcome); !filled) {
    return filled;
  }
  const uint64_t snapshot_tick = welcome.snapshot_tick;

  Envelope envelope;
  envelope.body.Set(std::move(welcome));
  if (const auto sent = session.Send(connection, Channel::kReliable, envelope); !sent) {
    return sent;
  }

  // The joiner ignores session deltas received before Welcome.
  const auto& deltas = world.get<ScheduledSessionDeltas>();
  const auto newer = std::ranges::upper_bound(deltas.history, snapshot_tick, {}, &ScheduledSessionDelta::apply_tick);
  if (const auto sent = SendSessionDeltas(session, connection, {newer, deltas.history.end()}); !sent) {
    return sent;
  }
  return SendSessionDeltas(session, connection, deltas.pending);
}

NetSession::Result SendRejected(NetSession& session, ConnectionId connection, std::string reason) {
  fbn::RejectedT rejected;
  rejected.reason = std::move(reason);

  Envelope envelope;
  envelope.body.Set(std::move(rejected));
  return session.Send(connection, Channel::kReliable, envelope);
}

NetSession::Result ScheduleSessionDelta(
    NetSession& session, flecs::world world, uint64_t apply_tick, SessionDelta delta) {
  world.get_mut<ScheduledSessionDeltas>().pending.push_back({.apply_tick = apply_tick, .delta = delta});

  Envelope envelope;
  std::visit([&envelope](auto& body) { envelope.body.Set(std::move(body)); }, delta);
  return session.Broadcast(Channel::kReliable, envelope);
}

// Spawned only to serialize it: like everywhere else, it exists from apply_tick on.
NetSession::Result SchedulePlayerJoined(NetSession& session, flecs::world world, uint32_t player_id) {
  const flecs::entity player = z13::gameplay::SpawnPlayer(world, player_id);
  fbn::PlayerJoinedT joined;
  joined.apply_tick = world.get<ft::SimulationClock>().tick + world.get<NetTuning>().session_event_delay_ticks;
  const auto entity_state = ft::CaptureEntityState(world, player);
  player.destruct();
  if (!entity_state) {
    return std::unexpected(entity_state.error());
  }
  joined.entity_state = ToWire(*entity_state);

  const uint64_t apply_tick = joined.apply_tick;
  return ScheduleSessionDelta(session, world, apply_tick, std::move(joined));
}

// Deltas apply before same-tick commands, so the leave waits past the last one.
uint64_t LeaveApplyTick(flecs::world world, uint32_t player_id) {
  auto after_own_commands = world.get<z13::gameplay::ScheduledCommands>().records |
      std::views::filter([player_id](const auto& record) { return record.player_id == player_id; }) |
      std::views::transform([](const auto& record) { return record.tick + 1; });
  return std::ranges::fold_left(
      after_own_commands, world.get<ft::SimulationClock>().tick + world.get<NetTuning>().session_event_delay_ticks,
      [](uint64_t a, uint64_t b) { return std::max(a, b); });
}

// Not state: a rollback restores IdCounters and would hand an id out twice.
struct PlayerIdAllocator {
  using Singleton = void;
  using SessionScoped = void;
  uint32_t next_player_id {};
};

std::expected<std::vector<uint32_t>, std::string> NegotiateActions(flecs::world world, const fbn::ClientHelloT& hello) {
  std::vector<zgi::ActionDescriptor> descriptors;
  descriptors.reserve(hello.actions.size());
  for (const auto& action : hello.actions) {
    if (!action) {
      return std::unexpected(std::string {"malformed action list"});
    }
    descriptors.push_back({.enum_name = action->enum_name, .value_name = action->value_name, .enum_value = action->enum_value});
  }
  return zgi::RegisterRemoteActions(world.get_mut<z13::input::ActionMap>(), world.get<NetTuning>(), descriptors);
}

NetSession::Result HandleClientHello(
    NetSession& session, flecs::world world, ConnectionId connection, const fbn::ClientHelloT& hello,
    z13::gameplay::IdCounters& counters) {
  if (hello.version != kProtocolVersion) {
    return SendRejected(session, connection, "protocol version mismatch");
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
    return SendRejected(session, connection, std::move(action_ids.error()));
  }

  // Post-increment (matches OnInit): last_player_id already holds the next id to hand
  // out, not the last one used.
  auto& allocator = world.get_mut<PlayerIdAllocator>();
  const uint32_t player_id = std::max(counters.last_player_id, allocator.next_player_id);
  counters.last_player_id = player_id + 1;
  allocator.next_player_id = player_id + 1;

  if (const auto bound = session.BindPlayer(connection, player_id); !bound) {
    return bound;
  }
  if (const auto welcomed = SendWelcome(session, world, connection, player_id, std::move(*action_ids)); !welcomed) {
    return welcomed;
  }
  return SchedulePlayerJoined(session, world, player_id);
}

NetSession::Result HandleResyncRequest(NetSession& session, flecs::world world, ConnectionId connection) {
  if (!session.PlayerIdFor(connection)) {
    log_warn("NetSession(server): ignoring a ResyncRequest from an unwelcomed connection {}", connection);
    return {};
  }
  log_info("NetSession(server): resyncing connection {}", connection);
  fbn::ResyncT resync;
  if (auto filled = FillCatchUp(world, resync); !filled) {
    return filled;
  }

  Envelope envelope;
  envelope.body.Set(std::move(resync));
  return session.Send(connection, Channel::kReliable, envelope);
}

NetSession::Result HandlePing(
    NetSession& session, flecs::world world, ConnectionId connection, const fbn::PingT& ping) {
  fbn::PongT pong;
  pong.client_tick = ping.client_tick;
  pong.server_tick = world.get<ft::SimulationClock>().tick;

  Envelope envelope;
  envelope.body.Set(std::move(pong));
  return session.Send(connection, Channel::kUnreliable, envelope);
}

bool IsKnownActionId(const z13::input::ActionMap& action_map, uint16_t action_id) {
  const auto& by_id = action_map.action_map.get<z13::input::ActionMap::IdTag>();
  return by_id.find(static_cast<z13::input::ActionInfo::IdType>(action_id)) != by_id.end();
}

struct ConnectionRateLimit {
  uint64_t window_start_tick {};
  uint32_t commands_this_window {};
};

struct CommandRateLimits {
  using Singleton = void;
  using SessionScoped = void;
  std::unordered_map<ConnectionId, ConnectionRateLimit> by_connection;
};

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

NetSession::Result HandleCommandBatch(
    NetSession& session, flecs::world world, ConnectionId connection, const fbn::CommandBatchT& batch) {
  const std::optional<uint32_t> player_id = session.PlayerIdFor(connection);
  if (!player_id) {
    log_warn("NetSession(server): dropping a CommandBatch from an unwelcomed connection {}", connection);
    return {};
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
    const uint64_t apply_tick = batch.base_tick + command.tick_delta();
    if (!IsWithinScheduleWindow(apply_tick, now, tuning) || (apply_tick < now && !CanReplayFrom(world, apply_tick))) {
      log_warn("NetSession(server): dropping a command for tick {} from connection {} at tick {}", apply_tick,
          connection, now);
      continue;
    }
    if (!AllowCommand(rate_limits, connection, now, tuning)) {
      log_warn("NetSession(server): connection {} exceeded its command rate limit, dropping the rest of this batch",
          connection);
      break;
    }
    QueueInOrder(queue, {
        .tick = apply_tick,
        .player_id = *player_id,
        .action_id = command.action_id(),
        .value = z13::gameplay::DequantizeActionValue(command.value()),
    });
    accepted.push_back(command);
  }
  if (accepted.empty()) {
    return {};
  }

  fbn::SequencedCommandsT sequenced;
  sequenced.player_id = *player_id;
  sequenced.base_tick = batch.base_tick;
  sequenced.commands = std::move(accepted);

  Envelope envelope;
  envelope.body.Set(std::move(sequenced));
  return session.Broadcast(Channel::kReliable, envelope, connection);
}

NetSession::Result HandleServerDisconnect(
    NetSession& session, flecs::world world, ConnectionId connection) {
  const std::optional<uint32_t> player_id = session.PlayerIdFor(connection);
  if (const auto removed = session.RemoveConnection(connection); !removed) {
    return removed;
  }
  world.get_mut<CommandRateLimits>().by_connection.erase(connection);
  if (!player_id) {
    return {};  // never completed the handshake
  }

  fbn::PlayerLeftT left;
  left.apply_tick = LeaveApplyTick(world, *player_id);
  left.player_id = *player_id;
  const uint64_t apply_tick = left.apply_tick;
  return ScheduleSessionDelta(session, world, apply_tick, std::move(left));
}

NetSession::Result HandleServerReceived(
    NetSession& session, flecs::world world, const TransportEvent& event, z13::gameplay::IdCounters& counters) {
  const auto decoded = DecodeMessage(AsUint8(event.data));
  if (!decoded) {
    log_warn("NetSession(server): dropping malformed packet from connection {}: {}", event.connection,
             decoded.error());
    // Disconnect() only notifies the peer, not us -- clean up our own bookkeeping the
    // same way a real kDisconnected event would.
    if (const auto dropped = session.Disconnect(event.connection); !dropped) {
      return dropped;
    }
    return HandleServerDisconnect(session, world, event.connection);
  }

  const ConnectionId connection = event.connection;
  return VisitBody(decoded->body, Overloaded {
      [&](const fbn::ClientHelloT& hello) { return HandleClientHello(session, world, connection, hello, counters); },
      [&](const fbn::PingT& ping) { return HandlePing(session, world, connection, ping); },
      [&](const fbn::ResyncRequestT&) { return HandleResyncRequest(session, world, connection); },
      [&](const fbn::CommandBatchT& batch) { return HandleCommandBatch(session, world, connection, batch); },
      [](const auto&) { return NetSession::Result {}; },
  });
}

NetSession::Result ServiceServerSession(
    flecs::world world, NetSession& session, z13::gameplay::IdCounters& counters) {
  const auto events = session.Service();
  if (!events) {
    return std::unexpected(events.error());
  }

  for (const TransportEvent& event : *events) {
    NetSession::Result handled;
    switch (event.kind) {
      case TransportEventKind::kConnected:
        handled = session.AddConnection(event.connection);
        break;
      case TransportEventKind::kDisconnected:
        handled = HandleServerDisconnect(session, world, event.connection);
        break;
      case TransportEventKind::kReceived:
        handled = HandleServerReceived(session, world, event, counters);
        break;
    }
    if (!handled) {
      return handled;
    }
  }
  return {};
}

// ---------- Client ----------

void SetConnectionStatus(flecs::world world, ConnectionState state, std::string reason = {}) {
  world.set<ConnectionStatus>({.state = state, .reason = std::move(reason)});
}

// A client's own net tuning, kept while it runs on its server's.
struct AdoptedSettings {
  using Singleton = void;
  NetTuning own_net;
  z13::PhysicsTuning own_physics;
};

void AdoptSessionSettings(flecs::world world, const z13::SessionSettings& session) {
  if (!world.has<AdoptedSettings>()) {
    world.set<AdoptedSettings>({.own_net = world.get<NetTuning>(), .own_physics = world.get<z13::PhysicsTuning>()});
  }
  z13::OverrideCoreFps(world, session.fps);
  world.set(NetTuning(session.net));
  world.set(z13::PhysicsTuning(session.physics));
}

void RestoreOwnSettings(flecs::world world) {
  if (!world.has<AdoptedSettings>()) {
    return;
  }
  const AdoptedSettings own = world.get<AdoptedSettings>();
  world.remove<AdoptedSettings>();
  world.set(own.own_net);
  world.set(own.own_physics);
  z13::OverrideCoreFps(world, std::nullopt);
}

// Invalidates any NetSession reference: ServiceNetSession suspends defer, so the remove
// is immediate. Callers must not touch it afterwards.
void EndSession(flecs::world world) {
  if (world.has<NetSession>()) {
    world.get_mut<NetSession>().Close();
    world.remove<NetSession>();
  }
  world.remove<ServerRole>();
  world.remove<ClientRole>();
  RestoreOwnSettings(world);
  // Queue systems keep running without a session and would apply the leftovers.
  ft::ResetSessionScopedComponents(world);
}

bool IsJoined(flecs::world world) {
  return world.has<ConnectionStatus>() && world.get<ConnectionStatus>().state == ConnectionState::kConnected;
}

struct CatchUpPayload {
  ft::WorldSnapshot snapshot;
  std::vector<z13::gameplay::PlayerActionRecord> actions;
  std::vector<z13::gameplay::PlayerActionRecord> held_values;
  std::vector<z13::gameplay::PlayerActionRecord> pending;
};

template <typename Message>
std::expected<CatchUpPayload, std::string> DecodeCatchUp(const Message& message) {
  if (!message.snapshot) {
    return std::unexpected(std::string {"no snapshot"});
  }
  return CatchUpPayload {
      .snapshot = ft::FromFlatbuffer(*message.snapshot),
      .actions = FromWire(message.actions),
      .held_values = FromWire(message.held_values),
      .pending = FromWire(message.pending),
  };
}

// Untrusted: an implausible span would spin the catch-up for max_catch_up_ticks_per_frame
// frames per tick, forever, with the transport unserviced.
bool IsPlausibleCatchUp(flecs::world world, uint64_t snapshot_tick, uint64_t target_tick) {
  return target_tick >= snapshot_tick &&
         target_tick - snapshot_tick <= world.get<z13::ActiveCoreSettings>().max_catch_up_ticks_per_frame;
}

// Replaces rather than merges: on a resync the local log, queue and snapshots are what
// diverged. The queue needs no merge: NetActionSender holds input back while awaiting it.
void AdoptCatchUp(flecs::world world, CatchUpPayload payload, uint64_t snapshot_tick, uint64_t target_tick) {
  std::vector<z13::gameplay::PlayerActionRecord> log_entries = std::move(payload.held_values);
  log_entries.insert(log_entries.end(), payload.actions.begin(), payload.actions.end());
  auto& log = world.get_mut<z13::gameplay::PlayerActionLog>().log;
  log.RemoveIf([](const auto&) { return true; });
  if (!log_entries.empty()) {
    log.MergeSorted(std::move(log_entries), RecordLess);
  }

  world.get_mut<z13::gameplay::ScheduledCommands>().records = std::move(payload.pending);

  auto& history = world.get_mut<ft::WorldSnapshotHistory>().history;
  history.RemoveIf([](const auto&) { return true; });
  history.Push({.tick = snapshot_tick, .snapshot = std::move(payload.snapshot)});

  auto& digests = world.get_mut<StateDigests>();
  digests.local.clear();
  digests.received.clear();
  digests.awaiting_resync = false;

  // A deferred rollback may reach past the adopted snapshot.
  world.get_mut<ft::RollbackRequest>() = {};
  ft::RequestRollback(world, snapshot_tick, target_tick);
}

// Validated together with this client's own retention: a server's windows must still fit its history.
std::expected<z13::SessionSettings, std::string> DecodeSessionSettings(flecs::world world, const fbn::WelcomeT& welcome) {
  if (!welcome.tuning || !welcome.physics) {
    return std::unexpected("server sent no settings");
  }
  const z13::SessionSettings session {.fps = welcome.fps, .net = *welcome.tuning, .physics = *welcome.physics};
  z13::Settings own = z13::MakeSettings();
  *own.core = world.get<z13::ActiveCoreSettings>();
  *own.net = world.get<NetTuning>();
  *own.physics = world.get<z13::PhysicsTuning>();
  if (const auto valid = z13::ValidateSettings(z13::WithSession(std::move(own), session)); !valid) {
    return std::unexpected(std::format("unusable server settings ({})", valid.error()));
  }
  return session;
}

void HandleWelcome(flecs::world world, const fbn::WelcomeT& welcome) {
  auto payload = DecodeCatchUp(welcome);
  if (!payload) {
    log_warn("NetSession(client): corrupt Welcome: {}", payload.error());
    SetConnectionStatus(world, ConnectionState::kFailed, "corrupt snapshot");
    EndSession(world);
    return;
  }
  const auto session_settings = DecodeSessionSettings(world, welcome);
  if (!session_settings) {
    log_warn("NetSession(client): unusable Welcome: {}", session_settings.error());
    SetConnectionStatus(world, ConnectionState::kFailed, session_settings.error());
    EndSession(world);
    return;
  }
  // Before the plausibility check, which is bounded by the server's catch-up limit.
  AdoptSessionSettings(world, *session_settings);
  world.set<ft::SnapshotCaptureRate>({.per_interval = world.get<NetTuning>().rollback_snapshots_per_interval});

  if (!IsPlausibleCatchUp(world, welcome.snapshot_tick, welcome.server_tick)) {
    SetConnectionStatus(world, ConnectionState::kFailed, "implausible server tick");
    EndSession(world);
    return;
  }

  if (const auto adopted = zgi::AdoptActionIds(world, welcome.action_ids); !adopted) {
    log_warn("NetSession(client): unusable Welcome: {}", adopted.error());
    SetConnectionStatus(world, ConnectionState::kFailed, adopted.error());
    EndSession(world);
    return;
  }

  world.set<z13::gameplay::LocalPlayer>({.id = welcome.player_id});
  world.remove<z13::gameplay::Pause>();
  world.add<z13::gameplay::Gameplay>();
  // The catch-up moves this clock onto the server's.
  world.set<ClockSync>({});

  // The join is an ordinary rollback; ConnectionStatus stays kConnecting until the
  // catch-up lands (see FinishPendingJoin).
  AdoptCatchUp(world, std::move(*payload), welcome.snapshot_tick, welcome.server_tick);
}

// Unlike a join, never moves this clock backwards: the replay also covers any ticks this
// client has already run past server_tick.
void HandleResync(flecs::world world, const fbn::ResyncT& resync) {
  auto payload = DecodeCatchUp(resync);
  const uint64_t target_tick = std::max(resync.server_tick, world.get<ft::SimulationClock>().tick);
  if (!payload || !IsPlausibleCatchUp(world, resync.snapshot_tick, target_tick)) {
    log_warn("NetSession(client): unusable Resync: {}", payload ? "implausible server tick" : payload.error());
    world.get_mut<StateDigests>().awaiting_resync = false;  // the next mismatch asks again
    return;
  }
  log_info("NetSession(client): resyncing from tick {}", resync.snapshot_tick);
  AdoptCatchUp(world, std::move(*payload), resync.snapshot_tick, target_tick);
  ++world.get_mut<StateDigests>().resyncs;
}

void HandleRejected(flecs::world world, const fbn::RejectedT& rejected) {
  SetConnectionStatus(world, ConnectionState::kFailed, rejected.reason);
  EndSession(world);
}

// Keeps IdCounters in step with the server, which bumped it at ClientHello.
void HandlePlayerJoined(flecs::world world, const fbn::PlayerJoinedT& joined) {
  if (!joined.entity_state) {
    log_warn("NetSession: PlayerJoined without the player's state");
    return;
  }
  if (auto applied = ft::ApplyWorldStateDelta(world, ft::FromFlatbuffer(*joined.entity_state)); !applied) {
    log_warn("NetSession: PlayerJoined delta rejected: {}", applied.error());
    return;
  }
  if (!world.has<z13::gameplay::IdCounters>()) {
    return;
  }
  auto& counters = world.get_mut<z13::gameplay::IdCounters>();
  world.query_builder<const z13::gameplay::Player>().build().each(
      [&counters](const z13::gameplay::Player& player) {
        counters.last_player_id = std::max(counters.last_player_id, player.id + 1);
      });
}

void HandlePlayerLeft(flecs::world world, const fbn::PlayerLeftT& left) {
  if (const flecs::entity e = world.lookup(z13::gameplay::PlayerEntityName(left.player_id).c_str())) {
    e.destruct();
  }
}

void ApplySessionDelta(flecs::world world, const SessionDelta& delta) {
  if (const auto* joined = std::get_if<fbn::PlayerJoinedT>(&delta)) {
    HandlePlayerJoined(world, *joined);
  } else if (const auto* left = std::get_if<fbn::PlayerLeftT>(&delta)) {
    HandlePlayerLeft(world, *left);
  }
}

void PruneSessionHistory(flecs::world world, uint64_t now, std::vector<ScheduledSessionDelta>& history) {
  const std::optional<uint64_t> retention_ticks = ft::SnapshotRetentionTicks(world);
  if (!retention_ticks) {
    return;
  }
  std::erase_if(history, [now, retention_ticks = *retention_ticks](const ScheduledSessionDelta& item) {
    return item.apply_tick + retention_ticks < now;
  });
}

// Every tick, replay included: a replayed tick re-applies its history.
void ApplyDueSessionDeltas(flecs::world world) {
  auto& scheduled = world.get_mut<ScheduledSessionDeltas>();
  const uint64_t now = world.get<ft::SimulationClock>().tick;

  // Stable, so same-tick deltas keep their arrival order in history.
  auto& pending = scheduled.pending;
  const auto not_due = std::ranges::stable_partition(
      pending, [now](const ScheduledSessionDelta& item) { return item.apply_tick <= now; });
  const auto due_now = std::ranges::subrange(pending.begin(), not_due.begin());
  const uint64_t earliest_due = std::ranges::fold_left(
      due_now | std::views::transform(&ScheduledSessionDelta::apply_tick), now,
      [](uint64_t a, uint64_t b) { return std::min(a, b); });
  std::ranges::for_each(due_now, [&history = scheduled.history](ScheduledSessionDelta& item) {
    const auto at = std::ranges::upper_bound(history, item.apply_tick, {}, &ScheduledSessionDelta::apply_tick);
    history.insert(at, std::move(item));
  });
  pending.erase(pending.begin(), not_due.begin());
  PruneSessionHistory(world, now, scheduled.history);

  // Applying creates/destroys entities, so not while iterating this component.
  const auto applied_now =
      std::ranges::equal_range(scheduled.history, now, {}, &ScheduledSessionDelta::apply_tick);
  std::vector<SessionDelta> due;
  std::ranges::transform(applied_now, std::back_inserter(due), &ScheduledSessionDelta::delta);
  std::ranges::for_each(due, [world](const SessionDelta& delta) { ApplySessionDelta(world, delta); });

  if (earliest_due < now && earliest_due > 0) {
    ft::RequestRollback(world, earliest_due - 1, now);
  }
}

// Runs once the world is back in the present; returns whether the session is still open.
bool FinishPendingJoin(flecs::world world) {
  if (world.has<ft::RollbackFailed>()) {
    const std::string reason = world.get<ft::RollbackFailed>().reason;
    world.remove<ft::RollbackFailed>();
    SetConnectionStatus(world, ConnectionState::kFailed, reason);
    EndSession(world);
    return false;
  }

  // LocalPlayer itself exists in every world from creation, so the id -- and the entity
  // it names, which arrives as a PlayerJoined delta -- is what says the join landed.
  if (!world.has<ConnectionStatus>() ||
      world.get<ConnectionStatus>().state != ConnectionState::kConnecting) {
    return true;
  }
  const std::optional<uint32_t> local_id = world.get<z13::gameplay::LocalPlayer>().id;
  if (local_id && world.lookup(z13::gameplay::PlayerEntityName(*local_id).c_str())) {
    // Do eagerly what SyncLocalPlayerListener would only do on the next frame.
    z13::gameplay::EnsureLocalPlayerReady(world);
    SetConnectionStatus(world, ConnectionState::kConnected);
  }
  return true;
}

void QueueSequencedCommands(flecs::world world, const fbn::SequencedCommandsT& sequenced) {
  auto& queue = world.get_mut<z13::gameplay::ScheduledCommands>();
  std::ranges::for_each(sequenced.commands, [&](const fbs::net::CommandWire& command) {
    QueueInOrder(queue, {
        .tick = sequenced.base_tick + command.tick_delta(),
        .player_id = sequenced.player_id,
        .action_id = command.action_id(),
        .value = z13::gameplay::DequantizeActionValue(command.value()),
    });
  });
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
      [&](const fbn::RejectedT& rejected) { HandleRejected(world, rejected); },
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

NetSession::Result SendClientHello(flecs::world world, NetSession& session, ConnectionId server_connection) {
  fbn::ClientHelloT hello;
  hello.version = kProtocolVersion;
  for (const zgi::ActionDescriptor& action : zgi::DescribeActions(world.get<z13::input::ActionMap>())) {
    auto described = std::make_unique<fbn::ActionDescT>();
    described->enum_name = action.enum_name;
    described->value_name = action.value_name;
    described->enum_value = action.enum_value;
    hello.actions.push_back(std::move(described));
  }

  Envelope envelope;
  envelope.body.Set(std::move(hello));
  return session.Send(server_connection, Channel::kReliable, envelope);
}

// A failed rollback after the join is one more way to diverge; during it, it's fatal
// (FinishPendingJoin).
NetSession::Result ResyncIfDiverged(flecs::world world, NetSession& session) {
  const std::optional<ConnectionId> server_connection = session.ServerConnection();
  if (!IsJoined(world) || !server_connection) {
    return {};
  }
  auto& digests = world.get_mut<StateDigests>();
  bool diverged = CheckReceivedStateDigests(world, digests);
  if (world.has<ft::RollbackFailed>()) {
    log_warn("NetSession(client): {}", world.get<ft::RollbackFailed>().reason);
    world.remove<ft::RollbackFailed>();
    diverged = true;
  }
  if (!diverged || digests.awaiting_resync) {
    return {};
  }

  digests.awaiting_resync = true;
  digests.received.clear();
  Envelope envelope;
  envelope.body.Set(fbn::ResyncRequestT {});
  return session.Send(*server_connection, Channel::kReliable, envelope);
}

// Unreliable: behind reliable traffic it would measure the queue, not the network.
NetSession::Result SendPingIfDue(flecs::world world, NetSession& session) {
  if (!world.has<ConnectionStatus>() || world.get<ConnectionStatus>().state != ConnectionState::kConnected) {
    return {};
  }
  const std::optional<ConnectionId> server_connection = session.ServerConnection();
  const std::optional<uint64_t> ticks_per_second = ft::TicksPerSecond(world);
  if (!server_connection || !ticks_per_second || *ticks_per_second == 0) {
    return {};
  }
  const uint64_t tick = world.get<ft::SimulationClock>().tick;
  if (tick % *ticks_per_second != 0) {
    return {};
  }

  fbn::PingT ping;
  ping.client_tick = tick;
  Envelope envelope;
  envelope.body.Set(std::move(ping));
  if (const auto sent = session.Send(*server_connection, Channel::kUnreliable, envelope); !sent) {
    return sent;
  }
  auto& sync = world.get_mut<ClockSync>();
  sync.ping_sent_tick = tick;
  sync.adjusted_ticks_in_flight = 0;
  return {};
}

NetSession::Result ServiceClientSession(flecs::world world, NetSession& session) {
  const auto events = session.Service();
  if (!events) {
    return std::unexpected(events.error());
  }

  for (const TransportEvent& event : *events) {
    switch (event.kind) {
      case TransportEventKind::kConnected: {
        if (const auto added = session.AddConnection(event.connection); !added) {
          return added;
        }
        if (const auto bound = session.SetServerConnection(event.connection); !bound) {
          return bound;
        }
        if (const auto greeted = SendClientHello(world, session, event.connection); !greeted) {
          return greeted;
        }
        break;
      }
      case TransportEventKind::kDisconnected:
        HandleClientDisconnect(world);
        return {};  // session/NetSession are gone; nothing left in `event`s applies
      case TransportEventKind::kReceived:
        HandleClientReceived(world, event);
        if (!world.has<NetSession>()) {
          return {};  // a rejected/corrupt Welcome closed the session; `session` is gone
        }
        break;
    }
  }
  return {};
}

// ---------- Lifecycle ----------

// Requests are consumed via OnSet, not OnAdd: OnAdd would run before .set() assigns the
// value, since add-then-assign is two observable steps in flecs.
void OnStartServerRequest(flecs::entity e, const StartServerRequest& request, const TransportFactories& factories) {
  flecs::world world = e.world();
  const uint16_t port = request.port;
  e.destruct();
  if (world.has<NetSession>()) {
    log_warn("NetSession: ignoring a StartServerRequest, a session is already open");
    return;
  }

  auto transport = factories.server(port);
  if (!transport) {
    log_critical("NetSession: failed to open the server on port {}: {}", port, transport.error());
    SetConnectionStatus(world, ConnectionState::kFailed, transport.error());
    // A dedicated server has no menu to fall back to.
    if (const auto config = z13::GetCoreConfig(world); config && config->get().IsServer()) {
      z13::ShutdownCore(world, EXIT_FAILURE);
    }
    return;
  }

  ft::ResetSessionScopedComponents(world);
  world.set<ft::SnapshotCaptureRate>({.per_interval = world.get<NetTuning>().rollback_snapshots_per_interval});
  NetSession session;
  session.Open(std::move(*transport));
  world.set<NetSession>(std::move(session));
  world.add<ServerRole>();
  SetConnectionStatus(world, ConnectionState::kConnected);
  world.remove<z13::gameplay::Pause>();
  world.add<z13::gameplay::Gameplay>();
}

void OnJoinRequest(flecs::entity e, const JoinRequest& request, const TransportFactories& factories) {
  flecs::world world = e.world();
  if (world.has<NetSession>()) {
    log_warn("NetSession: ignoring a JoinRequest, a session is already open");
    e.destruct();
    return;
  }
  ft::ResetSessionScopedComponents(world);
  world.set<ft::SnapshotCaptureRate>({.per_interval = world.get<NetTuning>().rollback_snapshots_per_interval});
  world.add<ClientRole>();
  SetConnectionStatus(world, ConnectionState::kConnecting);

  const z13::ConnectTimeoutConfig timeout = world.get<z13::ConnectTimeout>();
  auto transport = factories.client(request.endpoint, timeout);
  if (!transport) {
    SetConnectionStatus(world, ConnectionState::kFailed, transport.error());
    world.remove<ClientRole>();
    e.destruct();
    return;
  }

  NetSession session;
  session.Open(std::move(*transport));
  world.set<NetSession>(std::move(session));
  e.destruct();
}

void OnLeaveRequest(flecs::entity e) {
  flecs::world world = e.world();
  e.destruct();
  EndSession(world);
  SetConnectionStatus(world, ConnectionState::kNone);
}

void ServiceNetSession(flecs::world world) {
  // Suspends defer: .immediate() alone still defers this SaveState()'s writes.
  const z13::ImmediateScope immediate(world);

  ApplyDueSessionDeltas(world);

  if (!world.has<NetSession>()) {
    return;
  }

  // Exit to Main Menu only removes Gameplay; the session goes with it. A set local id
  // means Gameplay was already there (a client may still be catching up after Welcome).
  if (world.get<z13::gameplay::LocalPlayer>().id && !world.has<z13::gameplay::Gameplay>()) {
    EndSession(world);
    SetConnectionStatus(world, ConnectionState::kNone);
    return;
  }

  // The transport belongs to the present, not to ticks being re-simulated.
  if (ft::IsCatchingUp(world)) {
    return;
  }

  NetSession& session = world.get_mut<NetSession>();
  const bool is_server = world.has<ServerRole>();

  NetSession::Result serviced;
  if (is_server) {
    if (world.has<ft::RollbackFailed>()) {
      log_error("NetSession(server): {}", world.get<ft::RollbackFailed>().reason);
      world.remove<ft::RollbackFailed>();
    }
    serviced = ServiceServerSession(world, session, world.get_mut<z13::gameplay::IdCounters>());
    if (serviced) {
      serviced = SendSettledStateDigests(world, session, world.get_mut<StateDigests>());
    }
  } else {
    // Before servicing: digests received last frame are checked once the rollbacks their
    // preceding commands caused have landed.
    serviced = ResyncIfDiverged(world, session);
    if (serviced && FinishPendingJoin(world)) {
      serviced = ServiceClientSession(world, session);
      if (serviced && world.has<NetSession>()) {
        serviced = SendPingIfDue(world, session);
      }
    }
  }
  if (serviced) {
    return;
  }

  // Only a closed session reports here, which means this file's own sequencing is wrong.
  // Keeping the component would hide that and leave a half-dead session in the world.
  log_error("NetSession: {}", serviced.error());
  if (!is_server) {
    SetConnectionStatus(world, ConnectionState::kFailed, serviced.error());
  }
  EndSession(world);
}

void RegisterComponents(flecs::world world) {
  z13::flecs_tools::RegisterComponents<
      NetSession, ScheduledSessionDeltas, ClockSync, CommandRateLimits, PlayerIdAllocator, StateDigests,
      NetTuning, z13::ConnectTimeout, AdoptedSettings>(world);
}

void RegisterSystems(flecs::world world) {
  // Set here, not next to `.add(flecs::Singleton)` (see PhysicsSystem::RegisterSystems).
  world.set<ScheduledSessionDeltas>({});
  world.set<ClockSync>({});
  world.set<CommandRateLimits>({});
  world.set<PlayerIdAllocator>({});
  world.set<StateDigests>({});
  // The launcher overwrites these with the loaded settings (z13::InstallSettings).
  world.set<NetTuning>({});
  world.set<z13::ConnectTimeout>({});
  RegisterStateDigestSystems(world);

  world.observer<StartServerRequest, const TransportFactories>("NetSessionSystem::OnStartServerRequest")
      .event(flecs::OnSet)
      .each(OnStartServerRequest);

  world.observer<JoinRequest, const TransportFactories>("NetSessionSystem::OnJoinRequest")
      .event(flecs::OnSet)
      .each(OnJoinRequest);

  world.observer("NetSessionSystem::OnLeaveRequest")
      .with<LeaveRequest>()
      .event(flecs::OnAdd)
      .each(OnLeaveRequest);

  // PreFrame: earliest custom phase point, so a join/spawn/leave this tick is visible
  // to every gameplay system that runs later in the same frame.
  world.system("NetSessionSystem::Service")
      .kind(flecs::PreFrame)
      .immediate()
      .each([world]() { ServiceNetSession(world); });

  // PostFrame like the interval capture, so the snapshot's state and clock agree.
  world.system<ft::WorldSnapshotHistory, const ft::SimulationClock>("NetSessionSystem::EnsureBaselineSnapshot")
      .kind(flecs::PostFrame)
      .with<ServerRole>()
      .each([](flecs::iter& it, size_t, ft::WorldSnapshotHistory& history, const ft::SimulationClock& clock) {
        if (!history.history.Empty()) {
          return;
        }
        auto snapshot = ft::CaptureState(it.world());
        if (!snapshot) {
          log_error("NetSession(server): baseline snapshot failed: {}", snapshot.error());
          return;
        }
        history.history.Push({.tick = clock.tick, .snapshot = std::move(*snapshot)});
      });

  world.system<ClockSync, const ft::SimulationClock, const NetTuning>("NetSessionSystem::SteerClock")
      .kind(flecs::PostFrame)
      .with<ClientRole>()
      .without<ft::ReplayInProgress>()
      .each([](flecs::iter& it, size_t, ClockSync& sync, const ft::SimulationClock& clock, const NetTuning& tuning) {
        if (const int64_t adjust = TakeClockAdjustment(sync, tuning, clock.tick); adjust != 0) {
          flecs::world world = it.world();
          ft::RequestClockAdjust(world, adjust);
        }
      });
}

}  // namespace

void NetSessionSystem::Register(flecs::world& world) {
  z13::OnRegisterComponents(world, RegisterComponents);

  z13::OnInitSystems(world, RegisterSystems);
}

}  // namespace z13::net
