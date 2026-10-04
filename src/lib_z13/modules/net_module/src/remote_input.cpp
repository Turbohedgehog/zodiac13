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

#include "remote_input.h"

#include <algorithm>
#include <cstdint>
#include <vector>

#include <lib_core/state/rollback.h>
#include <lib_core/time/simulation_clock.h>
#include <lib_core/utils/flecs_utils.h>
#include <lib_core/world/components.h>
#include <lib_core/world/lifecycle.h>

#include <z13/components/input.h>
#include <z13/components/player_action.h>
#include <z13_settings/net_tuning.h>

#include "scheduled_commands.h"

namespace z13::net {

namespace {

namespace ft = z13::flecs_tools;

ActionValues ValuesAt(const z13::gameplay::PlayerActionLog& log, uint32_t player_id, uint64_t tick) {
  ActionValues values;
  for (const z13::gameplay::PlayerActionRecord& record : log.log.Entries()) {
    if (record.tick > tick) {
      break;
    }
    if (record.player_id == player_id) {
      values.insert_or_assign(record.action_id, record.value);
    }
  }
  return values;
}

// A late command is merged in and, batched with others, replayed from just before its tick.
void CommitDueCommands(
    flecs::iter& it, size_t, const ft::SimulationClock& clock, z13::gameplay::ScheduledCommands& scheduled,
    z13::gameplay::PlayerActionLog& log) {
  auto& records = scheduled.records;
  const auto due_end = std::ranges::find_if(
      records, [&clock](const z13::gameplay::PlayerActionRecord& record) { return record.tick > clock.tick; });
  if (due_end == records.begin()) {
    return;
  }
  std::vector<z13::gameplay::PlayerActionRecord> due(records.begin(), due_end);
  records.erase(records.begin(), due_end);

  const uint64_t earliest = due.front().tick;
  log.log.MergeSorted(std::move(due), RecordLess);
  if (earliest < clock.tick && earliest > 0) {
    flecs::world world = it.world();
    ft::DeferRollback(world, earliest - 1);
  }
}

void RegisterSystems(flecs::world world) {
  world.system<
      const ft::SimulationClock, z13::gameplay::ScheduledCommands, z13::gameplay::PlayerActionLog>(
      "RemoteInput::CommitDueCommands")
      .kind<z13::input::ScheduledCommandsPhase>()
      .each(CommitDueCommands);
}

}  // namespace

bool HoldsReleasableAction(const ActionValues& values, const z13::input::ActionMap& action_map) {
  const auto& by_id = action_map.action_map.get<z13::input::ActionMap::IdTag>();
  return std::ranges::any_of(values, [&by_id](const auto& entry) {
    const auto info = by_id.find(entry.first);
    return entry.second != 0.f && (info == by_id.end() || !info->absolute);
  });
}

bool ConfirmInputThrough(flecs::world world, uint32_t player_id, uint64_t through_tick) {
  if (world.get<NetTuning>().remote_input_prediction != fbs::net::RemoteInputPrediction::Neutral) {
    return false;
  }
  auto& confirmed = world.get_mut<z13::gameplay::ConfirmedInputTicks>().by_player;
  const auto [entry, inserted] = confirmed.try_emplace(player_id, through_tick);
  if (!inserted && through_tick <= entry->second) {
    return false;
  }
  // A first confirmation turns the ticks past it from held into released.
  const uint64_t predicted_since = inserted ? through_tick : entry->second;
  entry->second = through_tick;
  if (predicted_since < world.get<ft::SimulationClock>().tick &&
      HoldsReleasableAction(
          ValuesAt(world.get<z13::gameplay::PlayerActionLog>(), player_id, predicted_since),
          world.get<z13::input::ActionMap>())) {
    ft::DeferRollback(world, predicted_since);
  }
  return true;
}

void RemoteInput::Register(flecs::world& world) {
  OnInitSystems(world, RegisterSystems);
}

}  // namespace z13::net
