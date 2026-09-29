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

#include <lib_core/components.h>
#include <lib_core/flecs_utils.h>
#include <lib_core/lifecycle.h>
#include <lib_core/rollback.h>
#include <lib_core/simulation_clock.h>

#include <z13/components/input.h>
#include <z13/components/player_action.h>

#include "scheduled_commands.h"

namespace z13::net {

namespace {

namespace ft = z13::flecs_tools;

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

void RemoteInput::Register(flecs::world& world) {
  OnInitSystems(world, RegisterSystems);
}

}  // namespace z13::net
