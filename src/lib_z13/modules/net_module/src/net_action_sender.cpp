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
#include <ranges>
#include <string>

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
#include "wire_records.h"

namespace z13::net {

namespace {

namespace ft = z13::flecs_tools;
namespace fbn = fbs::net;

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

  auto run = ToCommandRun(outgoing.records, clock.tick);
  if (digests.awaiting_resync) {
    std::ranges::copy(outgoing.records, std::back_inserter(*digests.awaiting_resync));
  }
  outgoing.records.clear();
  if (!run) {
    return std::unexpected(std::format("dropping a batch: {}", run.error()));
  }
  fbn::CommandBatchT batch;
  batch.base_tick = run->base_tick;
  batch.commands = std::move(run->commands);
  batch.through_tick = clock.tick;
  Send(session, is_server, *local_player.id, std::move(batch));
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
