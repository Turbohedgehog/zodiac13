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

#include "net_action_sender.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

#include <lib_core/components.h>
#include <lib_core/log.h>
#include <lib_core/rollback.h>
#include <lib_core/simulation_clock.h>

#include <z13/components/net.h>
#include <z13/components/player_action.h>

#include <net_module/clock_sync.h>
#include <net_module/protocol.h>

#include "net_session.h"

namespace z13::net {

namespace {

namespace ft = z13::flecs_tools;
namespace fbn = fbs::net;

// One scheduled command, ready for the wire.
struct ScheduledCommand {
  uint64_t apply_tick {};
  uint8_t action_id {};
  int16_t value {};
};

std::vector<ScheduledCommand> Schedule(
    const std::vector<z13::gameplay::PlayerActionRecord>& records, const ClockSync& sync) {
  std::vector<ScheduledCommand> scheduled;
  scheduled.reserve(records.size());
  for (const z13::gameplay::PlayerActionRecord& record : records) {
    if (record.action_id > std::numeric_limits<uint8_t>::max()) {
      // CommandWire has one byte for it; truncating would move the command to a
      // different action on every other participant.
      log_error("NetActionSender: action id {} does not fit the wire format", record.action_id);
      continue;
    }
    scheduled.push_back({
        .apply_tick = ScheduleTick(record.tick, sync.offset_ticks),
        .action_id = static_cast<uint8_t>(record.action_id),
        .value = QuantizeActionValue(record.value),
    });
  }
  return scheduled;
}

fbn::CommandBatchT ToBatch(const std::vector<ScheduledCommand>& scheduled) {
  uint64_t base_tick = scheduled.front().apply_tick;
  for (const ScheduledCommand& command : scheduled) {
    base_tick = std::min(base_tick, command.apply_tick);
  }

  fbn::CommandBatchT batch;
  batch.base_tick = base_tick;
  for (const ScheduledCommand& command : scheduled) {
    const uint64_t delta = command.apply_tick - batch.base_tick;
    if (delta > std::numeric_limits<uint8_t>::max()) {
      // Can't happen with a send window of a few ticks; dropping beats misdating it.
      log_error("NetActionSender: command {} ticks past the batch base, dropping it", delta);
      continue;
    }
    batch.commands.emplace_back(static_cast<uint8_t>(delta), command.action_id, command.value);
  }
  return batch;
}

// Every kNetSendIntervalTicks, and never an empty batch: an idle client costs nothing
// beyond the transport's own keepalive.
void SendPendingCommands(
    flecs::iter& it, size_t, NetSession& session, const ClockSync& sync,
    const ft::SimulationClock& clock, z13::gameplay::OutgoingCommands& outgoing) {
  if (clock.tick % kNetSendIntervalTicks != 0 || outgoing.records.empty()) {
    return;
  }
  const std::optional<ConnectionId> server_connection = session.ServerConnection();
  if (!server_connection) {
    return;
  }

  const std::vector<ScheduledCommand> scheduled = Schedule(outgoing.records, sync);
  outgoing.records.clear();
  if (scheduled.empty()) {
    return;
  }

  Envelope envelope;
  envelope.body.Set(ToBatch(scheduled));
  if (const auto sent = session.Send(*server_connection, Channel::kReliable, envelope); !sent) {
    log_error("NetActionSender: {}", sent.error());
  }
}

// PostUpdate, like the log's own drain: this frame's intent is already buffered (see
// PhaseOrderTest), so a command goes out on the tick it was made.
void RegisterSystems(flecs::world world) {
  world.system<NetSession, const ClockSync, const ft::SimulationClock, z13::gameplay::OutgoingCommands>(
      "NetActionSender::SendPendingCommands")
      .kind(flecs::PostUpdate)
      .with<ClientRole>()
      .without<ft::ReplayInProgress>()
      .each(SendPendingCommands);
}

}  // namespace

void NetActionSender::Register(flecs::world& world) {
  world.observer<InitSystemsEvent>("NetActionSender::RegisterSystems")
      .event(flecs::OnAdd)
      .yield_existing()
      .each([world = world](const auto&) { RegisterSystems(world); });
}

}  // namespace z13::net
