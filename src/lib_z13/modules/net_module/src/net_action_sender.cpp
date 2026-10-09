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
#include <expected>
#include <format>
#include <iterator>
#include <limits>
#include <ranges>
#include <string>
#include <vector>

#include <lib_core/state/rollback.h>
#include <lib_core/time/simulation_clock.h>
#include <lib_core/utils/flecs_utils.h>
#include <lib_core/utils/log.h>
#include <lib_core/utils/status.h>
#include <lib_core/world/components.h>
#include <lib_core/world/lifecycle.h>

#include <z13/components/gameplay.h>
#include <z13/components/input.h>
#include <z13/components/net.h>
#include <z13/components/player_action.h>

#include <net_module/clock_sync.h>
#include <net_module/protocol.h>
#include <net_module/state_digest.h>

#include "net_session.h"
#include "remote_input.h"

namespace z13::net {

namespace {

namespace ft = z13::flecs_tools;
namespace fbn = fbs::net;

struct ScheduledCommand {
  uint64_t apply_tick {};
  uint16_t action_id {};
  int16_t value {};
};

std::expected<std::vector<ScheduledCommand>, std::string> ToScheduled(
    const std::vector<z13::gameplay::PlayerActionRecord>& records) {
  std::vector<ScheduledCommand> scheduled;
  scheduled.reserve(records.size());
  for (const z13::gameplay::PlayerActionRecord& record : records) {
    if (record.action_id > std::numeric_limits<uint16_t>::max()) {
      return std::unexpected(std::format("action id {} does not fit the wire format", record.action_id));
    }
    scheduled.push_back({
        .apply_tick = record.tick,
        .action_id = static_cast<uint16_t>(record.action_id),
        .value = z13::gameplay::QuantizeActionValue(record.value),
    });
  }
  return scheduled;
}

std::expected<fbn::CommandBatchT, std::string> ToBatch(
    const std::vector<ScheduledCommand>& scheduled, uint64_t through_tick) {
  uint64_t base_tick = scheduled.empty() ? through_tick : scheduled.front().apply_tick;
  for (const ScheduledCommand& command : scheduled) {
    base_tick = std::min(base_tick, command.apply_tick);
  }

  fbn::CommandBatchT batch;
  batch.base_tick = base_tick;
  batch.through_tick = through_tick;
  for (const ScheduledCommand& command : scheduled) {
    const uint64_t delta = command.apply_tick - batch.base_tick;
    if (delta > std::numeric_limits<uint8_t>::max()) {
      return std::unexpected(std::format("command {} ticks past the batch base", delta));
    }
    batch.commands.emplace_back(static_cast<uint8_t>(delta), command.action_id, command.value);
  }
  return batch;
}

// The client sends a CommandBatch; the host sequences its own commands on the spot,
// like any client's, and broadcasts them (its clock offset is 0).
void Send(NetSession& session, bool is_server, uint32_t player_id, fbn::CommandBatchT batch) {
  Envelope envelope;
  if (!is_server) {
    envelope.body.Set(std::move(batch));
    session.Send(*session.ServerConnection(), Channel::kReliable, envelope);
    return;
  }
  fbn::SequencedCommandsT sequenced;
  sequenced.player_id = player_id;
  sequenced.base_tick = batch.base_tick;
  sequenced.commands = std::move(batch.commands);
  sequenced.through_tick = batch.through_tick;
  envelope.body.Set(std::move(sequenced));
  session.Broadcast(Channel::kReliable, envelope);
}

Status TrySendPendingCommands(
    flecs::iter& it, size_t, NetSession& session, const ft::SimulationClock& clock, const z13::gameplay::LocalPlayer& local_player,
    StateDigests& digests, z13::gameplay::OutgoingCommands& outgoing,
    const z13::gameplay::LastRecordedActionValues& last_recorded, const z13::input::ActionMap& action_map) {
  const auto& tuning = it.world().get<NetTuning>();
  if (clock.tick % tuning.send_interval_ticks != 0) {
    return {};
  }
  const bool is_server = it.world().has<ServerRole>();
  if ((!is_server && !session.ServerConnection()) || !local_player.id) {
    return {};
  }
  // Observers release a held action past the last confirmed tick, so holding one is worth a batch.
  const bool heartbeat = HoldsReleasableAction(last_recorded.values, action_map);
  if (outgoing.records.empty() && !heartbeat) {
    return {};
  }

  const auto scheduled = ToScheduled(outgoing.records);
  if (digests.awaiting_resync) {
    std::ranges::copy(outgoing.records, std::back_inserter(*digests.awaiting_resync));
  }
  outgoing.records.clear();
  auto batch = scheduled.and_then(
      [&clock](const std::vector<ScheduledCommand>& commands) { return ToBatch(commands, clock.tick); });
  if (!batch) {
    return std::unexpected(std::format("dropping a batch: {}", batch.error()));
  }
  Send(session, is_server, *local_player.id, std::move(*batch));
  return {};
}

void SendPendingCommands(
    flecs::iter& it, size_t row, NetSession& session, const ft::SimulationClock& clock,
    const z13::gameplay::LocalPlayer& local_player, StateDigests& digests,
    z13::gameplay::OutgoingCommands& outgoing, const z13::gameplay::LastRecordedActionValues& last_recorded,
    const z13::input::ActionMap& action_map) {
  if (const auto sent = TrySendPendingCommands(
          it, row, session, clock, local_player, digests, outgoing, last_recorded, action_map);
      !sent) {
    log_error("NetActionSender: {}", sent.error());
  }
}

void RegisterSystems(flecs::world world) {
  world.system<
      NetSession, const ft::SimulationClock, const z13::gameplay::LocalPlayer,
      StateDigests, z13::gameplay::OutgoingCommands, const z13::gameplay::LastRecordedActionValues,
      const z13::input::ActionMap>(
      "NetActionSender::SendPendingCommands")
      .kind(flecs::PostUpdate)
      .without<ft::ReplayInProgress>()
      .each(SendPendingCommands);
}

}  // namespace

void NetActionSender::Register(flecs::world& world) {
  OnInitSystems(world, RegisterSystems);
}

}  // namespace z13::net
