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
#include <iterator>
#include <limits>
#include <optional>
#include <ranges>
#include <vector>

#include <boost/container/flat_map.hpp>

#include <lib_core/components.h>
#include <lib_core/flecs_utils.h>
#include <lib_core/lifecycle.h>
#include <lib_core/log.h>
#include <lib_core/rollback.h>
#include <lib_core/simulation_clock.h>

#include <z13/components/gameplay.h>
#include <z13/components/net.h>
#include <z13/components/player_action.h>

#include <net_module/clock_sync.h>
#include <net_module/protocol.h>
#include <net_module/state_digest.h>

#include "net_session.h"
#include "scheduled_commands.h"

namespace z13::net {

namespace {

namespace ft = z13::flecs_tools;
namespace fbn = fbs::net;

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
      log_error("NetActionSender: command {} ticks past the batch base, dropping it", delta);
      continue;
    }
    batch.commands.emplace_back(static_cast<uint8_t>(delta), command.action_id, command.value);
  }
  return batch;
}

// No echo: the sender queues its own copy on the same apply_tick.
void ScheduleLocally(
    flecs::world world, uint32_t player_id, const std::vector<ScheduledCommand>& scheduled) {
  auto& queue = world.get_mut<z13::gameplay::ScheduledCommands>();
  for (const ScheduledCommand& command : scheduled) {
    QueueInOrder(queue, {
        .tick = command.apply_tick,
        .player_id = player_id,
        .action_id = command.action_id,
        .value = DequantizeActionValue(command.value),
    });
  }
}

// Records older than one send interval were held back by a resync. Collapsed onto now,
// last value per action, so the wait adds no input delay and can't outrun the server's
// schedule window.
void CollapseHeldBackRecords(std::vector<z13::gameplay::PlayerActionRecord>& records, uint64_t now) {
  const uint64_t earliest = std::ranges::min(records, {}, &z13::gameplay::PlayerActionRecord::tick).tick;
  if (earliest + kNetSendIntervalTicks > now) {
    return;
  }
  boost::container::flat_map<z13::input::ActionInfo::IdType, z13::gameplay::PlayerActionRecord> latest;
  for (z13::gameplay::PlayerActionRecord record : records) {
    record.tick = now;
    latest.insert_or_assign(record.action_id, record);
  }
  records.clear();
  std::ranges::copy(latest | std::views::values, std::back_inserter(records));
}

void SendPendingCommands(
    flecs::iter& it, size_t, NetSession& session, const ClockSync& sync,
    const ft::SimulationClock& clock, const z13::gameplay::LocalPlayer& local_player,
    const StateDigests& digests, z13::gameplay::OutgoingCommands& outgoing) {
  // Held back, not dropped: everything sent before the ResyncRequest is in the Resync and
  // nothing after it may be, so the client can take the server's queue as-is.
  if (digests.awaiting_resync) {
    return;
  }
  if (clock.tick % kNetSendIntervalTicks != 0 || outgoing.records.empty()) {
    return;
  }
  const std::optional<ConnectionId> server_connection = session.ServerConnection();
  if (!server_connection || !local_player.id) {
    return;
  }

  CollapseHeldBackRecords(outgoing.records, clock.tick);
  const std::vector<ScheduledCommand> scheduled = Schedule(outgoing.records, sync);
  outgoing.records.clear();
  if (scheduled.empty()) {
    return;
  }
  ScheduleLocally(it.world(), *local_player.id, scheduled);

  Envelope envelope;
  envelope.body.Set(ToBatch(scheduled));
  if (const auto sent = session.Send(*server_connection, Channel::kReliable, envelope); !sent) {
    log_error("NetActionSender: {}", sent.error());
  }
}

void RegisterSystems(flecs::world world) {
  world.system<
      NetSession, const ClockSync, const ft::SimulationClock, const z13::gameplay::LocalPlayer,
      const StateDigests, z13::gameplay::OutgoingCommands>(
      "NetActionSender::SendPendingCommands")
      .kind(flecs::PostUpdate)
      .with<ClientRole>()
      .without<ft::ReplayInProgress>()
      .each(SendPendingCommands);
}

}  // namespace

void NetActionSender::Register(flecs::world& world) {
  OnInitSystems(world, RegisterSystems);
}

}  // namespace z13::net
