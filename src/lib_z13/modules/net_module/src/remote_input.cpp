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
#include <ranges>
#include <vector>

#include <lib_core/state/rollback.h>
#include <lib_core/time/simulation_clock.h>
#include <lib_core/utils/flecs_utils.h>
#include <lib_core/world/components.h>
#include <lib_core/world/lifecycle.h>

#include <z13/components/action_map.h>
#include <z13/components/input_phases.h>
#include <z13/components/player_action.h>

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

std::vector<z13::gameplay::PlayerActionRecord> ReleasesAfter(flecs::world world, uint32_t player_id,
                                                             uint64_t through_tick) {
  const auto& log = world.get<z13::gameplay::PlayerActionLog>();
  const auto& by_id = world.get<z13::input::ActionMap>().action_map.get<z13::input::ActionMap::IdTag>();
  const uint64_t release_tick = through_tick + 1;
  const auto at_release_tick =
      std::ranges::equal_range(log.log.Entries(), release_tick, {}, &z13::gameplay::PlayerActionRecord::tick);
  std::vector<z13::gameplay::PlayerActionRecord> releases;
  for (const auto& [action_id, value] : ValuesAt(log, player_id, through_tick)) {
    const auto info = by_id.find(action_id);
    const bool absolute = info != by_id.end() && info->absolute;
    const bool recorded_there = std::ranges::any_of(at_release_tick, [&](const auto& record) {
      return record.player_id == player_id && record.action_id == action_id;
    });
    if (value != 0.f && !absolute && !recorded_there) {
      releases.push_back({.tick = release_tick, .player_id = player_id, .action_id = action_id, .value = 0.f});
    }
  }
  return releases;
}

std::optional<uint64_t> AllConfirmedThrough(const z13::gameplay::ConfirmedInputTicks& confirmed) {
  if (confirmed.by_player.empty()) {
    return std::nullopt;
  }
  return std::ranges::min(confirmed.by_player | std::views::values);
}

bool ConfirmInputThrough(flecs::world world, uint32_t player_id, uint64_t through_tick) {
  auto& confirmed = world.get_mut<z13::gameplay::ConfirmedInputTicks>().by_player;
  const auto [entry, inserted] = confirmed.try_emplace(player_id, through_tick);
  if (!inserted && through_tick <= entry->second) {
    return false;
  }
  // A first confirmation turns the ticks past it from held into released.
  const uint64_t predicted_since = inserted ? through_tick : entry->second;
  entry->second = through_tick;
  const auto& log = world.get<z13::gameplay::PlayerActionLog>();
  // The newly confirmed ticks read the log from now on: what was held, and records already there
  // (moved commands, a server's releases), which they read as released until now.
  const auto& entries = log.log.Entries();
  const auto newly_confirmed = std::ranges::subrange(
      std::ranges::upper_bound(entries, predicted_since, {}, &z13::gameplay::PlayerActionRecord::tick),
      std::ranges::upper_bound(entries, through_tick, {}, &z13::gameplay::PlayerActionRecord::tick));
  const bool recorded_since =
      std::ranges::contains(newly_confirmed, player_id, &z13::gameplay::PlayerActionRecord::player_id);
  if (predicted_since < world.get<ft::SimulationClock>().tick &&
      (recorded_since ||
       HoldsReleasableAction(ValuesAt(log, player_id, predicted_since), world.get<z13::input::ActionMap>()))) {
    ft::DeferRollback(world, predicted_since);
  }
  return true;
}

void RemoteInput::Register(flecs::world& world) {
  OnInitSystems(world, RegisterSystems);
}

}  // namespace z13::net
