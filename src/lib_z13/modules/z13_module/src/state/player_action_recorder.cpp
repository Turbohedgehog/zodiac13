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

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

#include <boost/container/flat_map.hpp>

#include <flecs.h>

#include <lib_core/state/rollback.h>
#include <lib_core/state/world_snapshot_history.h>
#include <lib_core/state/world_state.h>
#include <lib_core/time/simulation_clock.h>
#include <lib_core/utils/flecs_utils.h>
#include <lib_core/world/components.h>
#include <lib_core/world/lifecycle.h>

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
// held-action state is never missing here (a rollback can't reach past it either). A pruned
// press still holds until its release, so its value moves to the oldest kept tick.
void PruneOldRecords(flecs::world world, uint64_t current_tick, z13::gameplay::PlayerActionLog& log) {
  const std::optional<uint64_t> ticks_per_second = z13::flecs_tools::TicksPerSecond(world);
  const std::optional<double> retention_seconds = z13::flecs_tools::SnapshotRetentionSeconds(world);
  if (!ticks_per_second || !retention_seconds) {
    return;
  }
  const uint64_t retention_ticks = RoundTicks(*retention_seconds, *ticks_per_second);
  log.retained_since_tick = current_tick > retention_ticks ? current_tick - retention_ticks : 0;

  const uint64_t retained_since = log.retained_since_tick;
  boost::container::flat_map<std::pair<uint32_t, z13::input::ActionInfo::IdType>, float> pruned;
  log.log.PruneOlderThan([retained_since, &pruned](const z13::gameplay::PlayerActionRecord& record) {
    if (record.tick >= retained_since) {
      return false;
    }
    pruned.insert_or_assign({record.player_id, record.action_id}, record.value);
    return true;
  });

  const auto& kept = log.log.Entries();
  const auto kept_at_oldest = std::ranges::subrange(
      kept.begin(), std::ranges::find_if(kept, [retained_since](const auto& record) { return record.tick != retained_since; }));
  std::vector<z13::gameplay::PlayerActionRecord> carried;
  for (const auto& [key, value] : pruned) {
    const auto& [player_id, action_id] = key;
    const bool overridden = std::ranges::any_of(kept_at_oldest, [&key](const auto& record) {
      return std::pair(record.player_id, record.action_id) == key;
    });
    if (value != 0.f && !overridden) {
      carried.push_back({.tick = retained_since, .player_id = player_id, .action_id = action_id, .value = value});
    }
  }
  log.log.MergeSorted(std::move(carried), z13::gameplay::RecordLess);
}

// Compares against last_recorded, not prev_value: see LastRecordedActionValues. The
// listener keeps the canonical value too, so live play matches the log it replays from.
void RecordChangedActions(
    flecs::iter& it, size_t,
    const z13::gameplay::Player& player,
    z13::input::ActionListener& action_listener,
    const z13::flecs_tools::SimulationClock& clock,
    z13::gameplay::OutgoingCommands& outgoing,
    z13::gameplay::LastRecordedActionValues& last_recorded) {
  const bool reassert = IsReassertTick(IntervalTicks(it.world()), clock.tick);

  for (auto& [action_id, holder] : action_listener.action_values) {
    const float current = z13::gameplay::CanonicalActionValue(*holder);
    *holder = current;
    float& last = last_recorded.values[action_id];
    if (current == last && !(reassert && current != 0.f)) {
      continue;
    }

    z13::gameplay::PlayerActionRecord record;
    record.tick = clock.tick;
    record.player_id = player.id;
    record.action_id = action_id;
    record.value = current;
    outgoing.records.push_back(record);
    last = current;
  }
}

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
      z13::gameplay::ScheduledCommands, z13::gameplay::LastRecordedActionValues>(world);
}

void RegisterSystems(flecs::world world) {
  // Set here, not next to `.add(flecs::Singleton)` (see PhysicsSystem::RegisterSystems).
  world.set<z13::gameplay::PlayerActionLog>({});
  world.set<z13::gameplay::OutgoingCommands>({});
  world.set<z13::gameplay::ScheduledCommands>({});
  world.set<z13::gameplay::LastRecordedActionValues>({});

  world.system<
      const z13::gameplay::Player, z13::input::ActionListener, const z13::flecs_tools::SimulationClock,
      z13::gameplay::OutgoingCommands, z13::gameplay::LastRecordedActionValues>(
      "PlayerActionRecorder::RecordChangedActions")
      .kind<z13::input::RecordActionFramePhase>()
      // Also runs on Pause, to record the release of held keys.
      .without<z13::flecs_tools::ReplayInProgress>()
      // Only the local player -- see the comment on RecordChangedActions above.
      .with<z13::input::CurrentActionListenerTag>()
      .each(RecordChangedActions);

  world.system<z13::gameplay::OutgoingCommands, z13::gameplay::PlayerActionLog>(
      "PlayerActionRecorder::ApplyOwnCommands")
      .kind(flecs::PostUpdate)
      .without<z13::net::ClientRole>()
      .without<z13::net::ServerRole>()
      .each(ApplyOwnCommands);

  world.system<const z13::flecs_tools::SimulationClock, z13::gameplay::PlayerActionLog>(
      "PlayerActionRecorder::PruneLog")
      .kind(flecs::PostUpdate)
      .each(PruneLog);
}

}  // namespace

void PlayerActionRecorder::Register(flecs::world& world) {
  OnRegisterComponents(world, RegisterComponents);

  OnInitSystems(world, RegisterSystems);
}

}  // namespace z13::state
