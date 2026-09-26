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
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <flecs.h>

#include <lib_core/components.h>
#include <lib_core/flecs_utils.h>
#include <lib_core/log.h>
#include <lib_core/rollback.h>
#include <lib_core/simulation_clock.h>
#include <lib_core/world_serializer.h>
#include <lib_core/world_snapshot_history.h>
#include <lib_core/world_state.h>

#include <z13/components/gameplay.h>
#include <z13/components/net.h>
#include <z13/components/player_action.h>
#include <z13_module/gameplay/gameplay_entities.h>

#include <net_module/clock_sync.h>
#include <net_module/protocol.h>

// reflect-cpp headers warn under /W4-as-errors; same suppression as world_serializer.cpp.
#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include <rfl/msgpack.hpp>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include "net_session.h"
#include "scheduled_commands.h"
#include "transport_factories.h"

namespace z13::net {

namespace {

namespace ft = z13::flecs_tools;
namespace fbn = fbs::net;

std::vector<uint8_t> ToBytes(const std::vector<char>& data) {
  return std::vector<uint8_t>(data.begin(), data.end());
}

std::vector<char> ToChars(const std::vector<uint8_t>& data) {
  return std::vector<char>(data.begin(), data.end());
}

// TransportEvent::data is std::byte; the codec layer wants uint8_t, and std::as_bytes
// only converts the other way.
std::span<const uint8_t> AsUint8(std::span<const std::byte> data) {
  return {reinterpret_cast<const uint8_t*>(data.data()), data.size()};
}

// The sender's own schedule is taken as given -- only sanitized, never recomputed, since
// the server doesn't know that client's clock offset. Stage 5 replays a command that is
// already late instead of nudging it forward like this.
uint64_t SanitizeApplyTick(uint64_t apply_tick, uint64_t now) {
  return std::clamp(apply_tick, now + 1, now + kMaxScheduleAheadTicks);
}

// ---------- Server ----------

// Every PlayerActionLog record after `since_tick`, oldest first (Entries() already is).
std::vector<z13::gameplay::PlayerActionRecord> ActionsSince(flecs::world world, uint64_t since_tick) {
  std::vector<z13::gameplay::PlayerActionRecord> actions;
  for (const auto& record : world.get<z13::gameplay::PlayerActionLog>().log.Entries()) {
    if (record.tick > since_tick) {
      actions.push_back(record);
    }
  }
  return actions;
}

// Every player's currently-held (action_id -> value), stamped at snapshot_tick so the
// joiner's log rebuild (AdvanceRecordedValues) picks it up as history predating `actions`
// above -- a hold that started before the snapshot leaves no record in the log itself.
std::vector<z13::gameplay::PlayerActionRecord> HeldValues(flecs::world world, uint64_t snapshot_tick) {
  std::vector<z13::gameplay::PlayerActionRecord> held;
  for (const auto& [key, value] : world.get<z13::gameplay::RemoteActionState>().current_values) {
    const auto& [player_id, action_id] = key;
    held.push_back({.tick = snapshot_tick, .player_id = player_id, .action_id = action_id, .value = value});
  }
  return held;
}

NetSession::Result SendWelcome(
    NetSession& session, flecs::world world, ConnectionId connection, uint32_t player_id) {
  fbn::WelcomeT welcome;
  welcome.player_id = player_id;
  welcome.server_tick = world.get<ft::SimulationClock>().tick;

  // Reuses WorldSnapshotHistory's cache instead of a fresh CaptureState() per join (too
  // expensive); the action batch lets the client catch up to server_tick by replaying it.
  const auto& history = world.get<ft::WorldSnapshotHistory>().history;
  if (history.Empty()) {
    // Nothing cached yet -- fall back to a fresh capture; nothing to replay either.
    welcome.snapshot = ToBytes(ft::SaveState(world));
    welcome.snapshot_tick = welcome.server_tick;
  } else {
    const ft::TimestampedSnapshot& latest = history.Entries().back();
    welcome.snapshot = ToBytes(ft::SaveState(latest.snapshot));
    welcome.snapshot_tick = latest.tick;
    welcome.actions = ToBytes(rfl::msgpack::write(ActionsSince(world, latest.tick)));
  }
  welcome.held_values = ToBytes(rfl::msgpack::write(HeldValues(world, welcome.snapshot_tick)));
  welcome.pending = ToBytes(rfl::msgpack::write(world.get<z13::gameplay::ScheduledCommands>().records));

  Envelope envelope;
  envelope.body.Set(std::move(welcome));
  return session.Send(connection, Channel::kReliable, envelope);
}

NetSession::Result SendRejected(NetSession& session, ConnectionId connection, std::string reason) {
  fbn::RejectedT rejected;
  rejected.reason = std::move(reason);

  Envelope envelope;
  envelope.body.Set(std::move(rejected));
  return session.Send(connection, Channel::kReliable, envelope);
}

NetSession::Result BroadcastPlayerJoined(NetSession& session, flecs::world world, flecs::entity player) {
  fbn::PlayerJoinedT joined;
  // The joiner's own catch-up replays forward to exactly this tick, so its own spawn is
  // due the moment that replay ends -- no separate path needed for "my own join".
  joined.apply_tick = world.get<ft::SimulationClock>().tick;
  joined.entity_state = ToBytes(ft::SaveEntityState(world, player));

  Envelope envelope;
  envelope.body.Set(std::move(joined));
  return session.Broadcast(Channel::kReliable, envelope);
}

// After the last command already scheduled for this player, so it plays out everywhere
// before the entity disappears -- not `now`, which could be earlier than that.
uint64_t LeaveApplyTick(flecs::world world, uint32_t player_id) {
  uint64_t latest = world.get<ft::SimulationClock>().tick;
  for (const auto& record : world.get<z13::gameplay::ScheduledCommands>().records) {
    if (record.player_id == player_id) {
      latest = std::max(latest, record.tick);
    }
  }
  return latest;
}

NetSession::Result BroadcastPlayerLeft(NetSession& session, ConnectionId except, fbn::PlayerLeftT left) {
  Envelope envelope;
  envelope.body.Set(std::move(left));
  return session.Broadcast(Channel::kReliable, envelope, except);
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

  // Post-increment (matches OnInit): last_player_id already holds the next id to hand
  // out, not the last one used.
  const uint32_t player_id = counters.last_player_id++;
  const flecs::entity player = z13::gameplay::SpawnPlayer(world, player_id);

  if (const auto bound = session.BindPlayer(connection, player_id); !bound) {
    return bound;
  }
  // Welcome first, then the spawn to everyone including the joiner: its snapshot is the
  // cached one from before this spawn, so the joiner learns of its own player exactly the
  // way the others do -- as a delta applied once its catch-up is done.
  if (const auto welcomed = SendWelcome(session, world, connection, player_id); !welcomed) {
    return welcomed;
  }
  return BroadcastPlayerJoined(session, world, player);
}

// server_tick is stamped on the way out rather than at the next frame boundary: the
// reply must not carry this frame's queueing delay as if it were time on the wire.
NetSession::Result HandlePing(
    NetSession& session, flecs::world world, ConnectionId connection, const fbn::PingT& ping) {
  fbn::PongT pong;
  pong.client_tick = ping.client_tick;
  pong.server_tick = world.get<ft::SimulationClock>().tick;

  Envelope envelope;
  envelope.body.Set(std::move(pong));
  return session.Send(connection, Channel::kUnreliable, envelope);
}

// Commands from a peer that hasn't been welcomed have no player to belong to, and a
// batch only reaches the others through here -- the sender applies its own copy.
NetSession::Result HandleCommandBatch(
    NetSession& session, flecs::world world, ConnectionId connection, const fbn::CommandBatchT& batch) {
  const std::optional<uint32_t> player_id = session.PlayerIdFor(connection);
  if (!player_id) {
    log_warn("NetSession(server): dropping a CommandBatch from an unwelcomed connection {}", connection);
    return {};
  }

  const uint64_t now = world.get<ft::SimulationClock>().tick;
  auto& queue = world.get_mut<z13::gameplay::ScheduledCommands>();

  fbn::SequencedCommandsT sequenced;
  sequenced.player_id = *player_id;
  sequenced.base_tick = batch.base_tick;
  for (const fbs::net::CommandWire& command : batch.commands) {
    const uint64_t apply_tick = SanitizeApplyTick(batch.base_tick + command.tick_delta(), now);
    QueueInOrder(queue, {
        .tick = apply_tick,
        .player_id = *player_id,
        .action_id = command.action_id(),
        .value = DequantizeActionValue(command.value()),
    });
    sequenced.commands.emplace_back(
        static_cast<uint8_t>(apply_tick - sequenced.base_tick), command.action_id(), command.value());
  }
  if (sequenced.commands.empty()) {
    return {};
  }

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
  if (!player_id) {
    return {};  // never completed the handshake
  }

  fbn::PlayerLeftT left;
  left.apply_tick = LeaveApplyTick(world, *player_id);
  left.player_id = *player_id;
  // Scheduled like every other participant's copy, not destructed immediately: this
  // player's own already-queued commands (RemoteInput hasn't drained them yet) must
  // still apply here too before the entity disappears.
  world.get_mut<ScheduledSessionDeltas>().pending.push_back({.apply_tick = left.apply_tick, .delta = left});
  return BroadcastPlayerLeft(session, connection, left);
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

  if (const auto* hello = AsBody<fbn::ClientHelloT>(decoded->body)) {
    return HandleClientHello(session, world, event.connection, *hello, counters);
  }
  if (const auto* ping = AsBody<fbn::PingT>(decoded->body)) {
    return HandlePing(session, world, event.connection, *ping);
  }
  if (const auto* batch = AsBody<fbn::CommandBatchT>(decoded->body)) {
    return HandleCommandBatch(session, world, event.connection, *batch);
  }
  // ResyncRequest isn't handled until a later stage; ignore.
  return {};
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

// Invalidates `session`: ServiceNetSession suspends defer, so the remove is immediate.
// Callers must not touch it afterwards.
void CloseClientSession(flecs::world world, NetSession& session) {
  session.Close();
  world.remove<NetSession>();
}

// actions/held_values/pending can legitimately be empty (nothing to report, or the "no
// cache yet" fallback); msgpack can't decode zero bytes, so short-circuit that case.
std::expected<std::vector<z13::gameplay::PlayerActionRecord>, std::string> DecodeRecords(
    const std::vector<char>& bytes) {
  if (bytes.empty()) {
    return std::vector<z13::gameplay::PlayerActionRecord> {};
  }
  auto result = rfl::msgpack::read<std::vector<z13::gameplay::PlayerActionRecord>>(bytes);
  if (!result) {
    return std::unexpected(result.error().what());
  }
  return *result;
}

void HandleWelcome(flecs::world world, NetSession& session, const fbn::WelcomeT& welcome) {
  auto snapshot = rfl::msgpack::read<ft::WorldSnapshot>(ToChars(welcome.snapshot));
  auto actions = DecodeRecords(ToChars(welcome.actions));
  auto held_values = DecodeRecords(ToChars(welcome.held_values));
  auto pending = DecodeRecords(ToChars(welcome.pending));
  if (!snapshot || !actions || !held_values || !pending) {
    SetConnectionStatus(world, ConnectionState::kFailed, "corrupt snapshot");
    CloseClientSession(world, session);
    return;
  }

  // Untrusted: an implausible span would spin the catch-up for kMaxCatchUpTicksPerFrame
  // frames per tick, forever, with the transport unserviced.
  if (welcome.server_tick < welcome.snapshot_tick ||
      welcome.server_tick - welcome.snapshot_tick > ft::kMaxCatchUpTicksPerFrame) {
    SetConnectionStatus(world, ConnectionState::kFailed, "implausible server tick");
    CloseClientSession(world, session);
    return;
  }

  world.set<z13::gameplay::LocalPlayer>({.id = welcome.player_id});
  world.remove<z13::gameplay::Pause>();
  world.add<z13::gameplay::Gameplay>();

  // held_values predates actions (both are tick-sorted; see HeldValues on the server) --
  // merging them together keeps the log's own sort invariant with a single call.
  std::vector<z13::gameplay::PlayerActionRecord> log_entries = std::move(*held_values);
  log_entries.insert(log_entries.end(), actions->begin(), actions->end());
  if (!log_entries.empty()) {
    world.get_mut<z13::gameplay::PlayerActionLog>().log.MergeSorted(std::move(log_entries), RecordLess);
  }

  if (!pending->empty()) {
    auto& queue = world.get_mut<z13::gameplay::ScheduledCommands>();
    for (const z13::gameplay::PlayerActionRecord& record : *pending) {
      QueueInOrder(queue, record);
    }
  }

  // The join is an ordinary rollback; ConnectionStatus stays kConnecting until the
  // catch-up lands (see FinishPendingJoin).
  world.get_mut<ft::WorldSnapshotHistory>().history.Push(
      {.tick = welcome.snapshot_tick, .snapshot = std::move(*snapshot)});
  ft::RequestRollback(world, welcome.snapshot_tick, welcome.server_tick);
}

void HandleRejected(flecs::world world, NetSession& session, const fbn::RejectedT& rejected) {
  SetConnectionStatus(world, ConnectionState::kFailed, rejected.reason);
  CloseClientSession(world, session);
}

void HandlePlayerJoined(flecs::world world, const fbn::PlayerJoinedT& joined) {
  if (auto applied = ft::ApplyWorldStateDelta(world, ToChars(joined.entity_state)); !applied) {
    log_warn("NetSession(client): PlayerJoined delta rejected: {}", applied.error());
  }
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

// Runs every tick, replay included: the joiner's own spawn can be due on the very last
// tick its own catch-up replays, and ServiceNetSession skips everything else during
// catch-up (see below), so this can't wait for that.
void ApplyDueSessionDeltas(flecs::world world) {
  auto& scheduled = world.get_mut<ScheduledSessionDeltas>();
  if (scheduled.pending.empty()) {
    return;
  }
  const uint64_t now = world.get<ft::SimulationClock>().tick;
  std::vector<ScheduledSessionDelta> remaining;
  for (auto& item : scheduled.pending) {
    if (item.apply_tick <= now) {
      ApplySessionDelta(world, item.delta);
    } else {
      remaining.push_back(std::move(item));
    }
  }
  scheduled.pending = std::move(remaining);
}

// Runs once the world is back in the present; returns whether the session is still open.
bool FinishPendingJoin(flecs::world world, NetSession& session) {
  if (world.has<ft::RollbackFailed>()) {
    const std::string reason = world.get<ft::RollbackFailed>().reason;
    world.remove<ft::RollbackFailed>();
    world.get_mut<ScheduledSessionDeltas>().pending.clear();
    SetConnectionStatus(world, ConnectionState::kFailed, reason);
    CloseClientSession(world, session);
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

void HandleClientReceived(flecs::world world, NetSession& session, const TransportEvent& event) {
  const auto decoded = DecodeMessage(AsUint8(event.data));
  if (!decoded) {
    log_warn("NetSession(client): dropping malformed packet: {}", decoded.error());
    return;
  }

  if (const auto* welcome = AsBody<fbn::WelcomeT>(decoded->body)) {
    HandleWelcome(world, session, *welcome);
  } else if (const auto* rejected = AsBody<fbn::RejectedT>(decoded->body)) {
    HandleRejected(world, session, *rejected);
  } else if (const auto* pong = AsBody<fbn::PongT>(decoded->body)) {
    ApplyPong(world.get_mut<ClockSync>(), pong->client_tick, pong->server_tick,
              world.get<ft::SimulationClock>().tick);
  } else if (const auto* sequenced = AsBody<fbn::SequencedCommandsT>(decoded->body)) {
    auto& queue = world.get_mut<z13::gameplay::ScheduledCommands>();
    for (const fbs::net::CommandWire& command : sequenced->commands) {
      QueueInOrder(queue, {
          .tick = sequenced->base_tick + command.tick_delta(),
          .player_id = sequenced->player_id,
          .action_id = command.action_id(),
          .value = DequantizeActionValue(command.value()),
      });
    }
  } else if (const auto* joined = AsBody<fbn::PlayerJoinedT>(decoded->body)) {
    world.get_mut<ScheduledSessionDeltas>().pending.push_back({.apply_tick = joined->apply_tick, .delta = *joined});
  } else if (const auto* left = AsBody<fbn::PlayerLeftT>(decoded->body)) {
    world.get_mut<ScheduledSessionDeltas>().pending.push_back({.apply_tick = left->apply_tick, .delta = *left});
  }
  // StateDigest isn't handled until a later stage; ignore.
}

void HandleClientDisconnect(flecs::world world, NetSession& session) {
  const bool was_connected = world.has<z13::gameplay::Gameplay>();
  CloseClientSession(world, session);

  if (was_connected) {
    world.remove<z13::gameplay::Gameplay>();
    world.add<z13::gameplay::Pause>();
    SetConnectionStatus(world, ConnectionState::kDisconnected, "server closed the connection");
  } else {
    SetConnectionStatus(world, ConnectionState::kFailed, "disconnected before joining");
  }
}

NetSession::Result SendClientHello(NetSession& session, ConnectionId server_connection) {
  fbn::ClientHelloT hello;
  hello.version = kProtocolVersion;

  Envelope envelope;
  envelope.body.Set(std::move(hello));
  return session.Send(server_connection, Channel::kReliable, envelope);
}

// One Ping a second, on the unreliable channel: queued behind the reliable traffic it
// would measure the queue rather than the network. A lost one just costs this second's
// sample -- the next Ping replaces the one in flight.
NetSession::Result SendPingIfDue(flecs::world world, NetSession& session) {
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
  world.get_mut<ClockSync>().ping_sent_tick = tick;
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
        if (const auto greeted = SendClientHello(session, event.connection); !greeted) {
          return greeted;
        }
        break;
      }
      case TransportEventKind::kDisconnected:
        HandleClientDisconnect(world, session);
        return {};  // session/NetSession are gone; nothing left in `event`s applies
      case TransportEventKind::kReceived:
        HandleClientReceived(world, session, event);
        if (!world.has<NetSession>()) {
          return {};  // a rejected/corrupt Welcome closed the session; `session` is gone
        }
        break;
    }
  }
  return {};
}

// ---------- Lifecycle ----------

// Eager rather than lazy: on InMemoryTransport a connect attempt only resolves against
// whatever is already listening, so the server must bind first.
void OnServerRoleAdded(flecs::iter it, size_t /*i*/, ServerRole, const TransportFactories& factories) {
  flecs::world world = it.world();
  const auto config = z13::GetCoreConfig(world);
  const uint16_t port = config ? config->get().GetPort() : z13::kDefaultServerPort;
  auto transport = factories.server(port);
  if (!transport) {
    log_critical("NetSession: failed to open the server on port {}: {}", port, transport.error());
    world.set<ConnectionStatus>({.state = ConnectionState::kFailed, .reason = transport.error()});
    return;
  }

  NetSession session;
  session.OpenAsServer(std::move(*transport));
  world.set<NetSession>(std::move(session));
  world.set<ConnectionStatus>({.state = ConnectionState::kConnected});
}

void OnServerRoleRemoved(flecs::iter it, size_t /*i*/, ServerRole) {
  flecs::world world = it.world();
  if (world.has<NetSession>()) {
    world.get_mut<NetSession>().Close();
    world.remove<NetSession>();
  }
}

// Consumed via OnSet, not OnAdd: OnAdd would run before .set() assigns the Endpoint,
// since add-then-assign is two observable steps in flecs.
void OnJoinRequest(flecs::entity e, const JoinRequest& request, const TransportFactories& factories) {
  flecs::world world = e.world();
  SetConnectionStatus(world, ConnectionState::kConnecting);

  const auto config = z13::GetCoreConfig(world);
  const z13::ConnectTimeoutConfig timeout = config ? config->get().GetConnectTimeout() : z13::ConnectTimeoutConfig {};
  auto transport = factories.client(request.endpoint, timeout);
  if (!transport) {
    SetConnectionStatus(world, ConnectionState::kFailed, transport.error());
    e.destruct();
    return;
  }

  NetSession session;
  session.OpenAsClient(std::move(*transport));
  world.set<NetSession>(std::move(session));
  e.destruct();
}

void ServiceNetSession(flecs::world world) {
  // .immediate() only gives the real (non-staged) world; add/set/remove through it are
  // still deferred, so defer must also be suspended here for this call's own SaveState().
  const z13::ImmediateScope immediate(world);

  // Every tick, replay included -- unlike everything below, which only concerns the
  // live transport and is skipped while catching up (see ApplyDueSessionDeltas).
  ApplyDueSessionDeltas(world);

  if (!world.has<NetSession>()) {
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
    serviced = ServiceServerSession(world, session, world.get_mut<z13::gameplay::IdCounters>());
  } else if (FinishPendingJoin(world, session)) {
    serviced = ServiceClientSession(world, session);
    if (serviced && world.has<NetSession>()) {
      serviced = SendPingIfDue(world, session);
    }
  }
  if (serviced) {
    return;
  }

  // Only a closed session reports here, which means this file's own sequencing is wrong.
  // Keeping the component would hide that and leave a half-dead session in the world.
  log_error("NetSession: {}", serviced.error());
  if (is_server) {
    world.remove<NetSession>();
  } else if (world.has<NetSession>()) {
    SetConnectionStatus(world, ConnectionState::kFailed, serviced.error());
    CloseClientSession(world, world.get_mut<NetSession>());
  }
}

void RegisterComponents(flecs::world world) {
  z13::flecs_tools::RegisterComponents<NetSession, ScheduledSessionDeltas, ClockSync>(world);
}

void RegisterSystems(flecs::world world) {
  // Set here, not next to `.add(flecs::Singleton)` (see PhysicsSystem::RegisterSystems).
  world.set<ScheduledSessionDeltas>({});
  world.set<ClockSync>({});

  world.observer<ServerRole, const TransportFactories>("NetSessionSystem::OnServerRoleAdded")
      .event(flecs::OnAdd)
      .yield_existing()
      .each(OnServerRoleAdded);

  world.observer<ServerRole>("NetSessionSystem::OnServerRoleRemoved")
      .event(flecs::OnRemove)
      .each(OnServerRoleRemoved);

  world.observer<JoinRequest, const TransportFactories>("NetSessionSystem::OnJoinRequest")
      .event(flecs::OnSet)
      .each(OnJoinRequest);

  // PreFrame: earliest custom phase point, so a join/spawn/leave this tick is visible
  // to every gameplay system that runs later in the same frame.
  world.system("NetSessionSystem::Service")
      .kind(flecs::PreFrame)
      .immediate()
      .each([world]() { ServiceNetSession(world); });
}

}  // namespace

void NetSessionSystem::Register(flecs::world& world) {
  world.observer<z13::RegisterComponentsEvent>("NetSessionSystem::RegisterComponents")
      .event(flecs::OnAdd)
      .yield_existing()
      .each([world = world](const auto&) { RegisterComponents(world); });

  world.observer<z13::InitSystemsEvent>("NetSessionSystem::RegisterSystems")
      .event(flecs::OnAdd)
      .yield_existing()
      .each([world = world](const auto&) { RegisterSystems(world); });
}

}  // namespace z13::net
