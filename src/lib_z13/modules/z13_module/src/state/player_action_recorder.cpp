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

#include "player_action_recorder.h"

#include <cmath>
#include <cstddef>
#include <optional>

#include <flecs.h>

#include <lib_core/components.h>
#include <lib_core/rollback.h>
#include <lib_core/simulation_clock.h>
#include <lib_core/world_snapshot_history.h>
#include <lib_core/world_state.h>

#include <z13/components/gameplay.h>
#include <z13/components/input.h>
#include <z13/components/net.h>
#include <z13/components/player_action.h>

namespace z13::state {

namespace {

uint64_t RoundTicks(double seconds, uint64_t ticks_per_second) {
  return static_cast<uint64_t>(std::llround(seconds * static_cast<double>(ticks_per_second)));
}

std::optional<uint64_t> IntervalTicks(flecs::world world) {
  const std::optional<uint64_t> ticks_per_second = z13::flecs_tools::TicksPerSecond(world);
  const std::optional<double> interval_seconds = z13::flecs_tools::SnapshotIntervalSeconds(world);
  if (!ticks_per_second || !interval_seconds) {
    return std::nullopt;
  }
  return RoundTicks(*interval_seconds, *ticks_per_second);
}

// A held action only changes on press/release, so a snapshot taken mid-hold would leave
// replay unaware it was active. Re-asserting nonzero values on the same cadence as
// WorldSnapshotHistory's captures fixes that.
bool IsReassertTick(const std::optional<uint64_t>& interval_ticks, uint64_t tick) {
  return interval_ticks && *interval_ticks != 0 && tick % *interval_ticks == 0;
}

// Mirrors WorldSnapshotHistory's own retention window, so a still-retained snapshot's
// held-action state is never missing here (a rollback can't reach past it either).
void PruneOldRecords(flecs::world world, uint64_t current_tick, z13::gameplay::PlayerActionLog& log) {
  const std::optional<uint64_t> ticks_per_second = z13::flecs_tools::TicksPerSecond(world);
  const std::optional<double> retention_seconds = z13::flecs_tools::SnapshotRetentionSeconds(world);
  if (!ticks_per_second || !retention_seconds) {
    return;
  }
  const uint64_t retention_ticks = RoundTicks(*retention_seconds, *ticks_per_second);
  log.retained_since_tick = current_tick > retention_ticks ? current_tick - retention_ticks : 0;

  const uint64_t retained_since = log.retained_since_tick;
  log.log.PruneOlderThan([retained_since](const z13::gameplay::PlayerActionRecord& record) {
    return record.tick < retained_since;
  });
}

// Only the local player (CurrentActionListenerTag) is recorded -- a remote player's
// entity is driven by the replay injecting from the log, so recording it too would be
// redundant.
void RecordChangedActions(
    flecs::iter& it, size_t,
    const z13::gameplay::Player& player,
    const z13::input::ActionListener& action_listener,
    const z13::flecs_tools::SimulationClock& clock,
    z13::gameplay::OutgoingCommands& outgoing) {
  const bool reassert = IsReassertTick(IntervalTicks(it.world()), clock.tick);

  for (const auto& [action_id, holder] : action_listener.action_values) {
    if (!holder.HasBeenChanged() && !(reassert && *holder != 0.f)) {
      continue;
    }

    z13::gameplay::PlayerActionRecord record;
    record.tick = clock.tick;
    record.player_id = player.id;
    record.action_id = action_id;
    record.value = *holder;
    outgoing.records.push_back(record);
  }
}

// Off a client, intent is applied the moment it is made, so it goes straight into the
// log. A client's copy goes on the wire instead and comes back scheduled (NetActionSender).
void ApplyOwnCommands(
    flecs::iter&, size_t, z13::gameplay::OutgoingCommands& outgoing,
    z13::gameplay::PlayerActionLog& log) {
  for (const z13::gameplay::PlayerActionRecord& record : outgoing.records) {
    log.log.Push(record);
  }
  outgoing.records.clear();
}

void PruneLog(
    flecs::iter& it, size_t, const z13::flecs_tools::SimulationClock& clock,
    z13::gameplay::PlayerActionLog& log) {
  PruneOldRecords(it.world(), clock.tick, log);
}

void RegisterComponents(flecs::world world) {
  z13::flecs_tools::RegisterComponents<
      z13::gameplay::PlayerActionLog, z13::gameplay::OutgoingCommands,
      z13::gameplay::ScheduledCommands>(world);
}

void RegisterSystems(flecs::world world) {
  // Set here, not next to `.add(flecs::Singleton)` (see PhysicsSystem::RegisterSystems).
  world.set<z13::gameplay::PlayerActionLog>({});
  world.set<z13::gameplay::OutgoingCommands>({});
  world.set<z13::gameplay::ScheduledCommands>({});

  // RecordActionFramePhase: right after Calculate, before RemoteActionFramePhase can
  // overwrite ActionListener with a delayed value.
  world.system<
      const z13::gameplay::Player, const z13::input::ActionListener, const z13::flecs_tools::SimulationClock,
      z13::gameplay::OutgoingCommands>(
      "PlayerActionRecorder::RecordChangedActions")
      .kind<z13::input::RecordActionFramePhase>()
      .without<z13::gameplay::Pause>()
      // Don't re-log what the replay is injecting from the log -- see replay.cpp.
      .without<z13::flecs_tools::ReplayInProgress>()
      // Only the local player -- see the comment on RecordChangedActions above.
      .with<z13::input::CurrentActionListenerTag>()
      .each(RecordChangedActions);

  // PostUpdate: strictly after ApplyActionFramePhase (see PhaseOrderTest), so this
  // frame's intent is already in the buffer.
  world.system<z13::gameplay::OutgoingCommands, z13::gameplay::PlayerActionLog>(
      "PlayerActionRecorder::ApplyOwnCommands")
      .kind(flecs::PostUpdate)
      .without<z13::net::ClientRole>()
      .each(ApplyOwnCommands);

  world.system<const z13::flecs_tools::SimulationClock, z13::gameplay::PlayerActionLog>(
      "PlayerActionRecorder::PruneLog")
      .kind(flecs::PostUpdate)
      .each(PruneLog);
}

}  // namespace

void PlayerActionRecorder::Register(flecs::world& world) {
  world.observer<RegisterComponentsEvent>("PlayerActionRecorder::RegisterComponents")
      .event(flecs::OnAdd)
      .yield_existing()
      .each([world = world](const auto&) { RegisterComponents(world); });

  world.observer<InitSystemsEvent>("PlayerActionRecorder::RegisterSystems")
      .event(flecs::OnAdd)
      .yield_existing()
      .each([world = world](const auto&) { RegisterSystems(world); });
}

}  // namespace z13::state
