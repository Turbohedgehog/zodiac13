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

#include <lib_core/components.h>
#include <lib_core/simulation_clock.h>

#include <z13/components/input.h>
#include <z13/components/player_action.h>

namespace z13::net {

namespace {

namespace ft = z13::flecs_tools;

// ScheduledCommands is kept sorted by tick, so due entries are always a prefix.
void CommitDueCommands(
    flecs::iter&, size_t, const ft::SimulationClock& clock, z13::gameplay::ScheduledCommands& scheduled,
    z13::gameplay::PlayerActionLog& log) {
  auto& records = scheduled.records;
  auto due = records.begin();
  while (due != records.end() && due->tick <= clock.tick) {
    log.log.Push(*due);
    ++due;
  }
  records.erase(records.begin(), due);
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
  world.observer<InitSystemsEvent>("RemoteInput::RegisterSystems")
      .event(flecs::OnAdd)
      .yield_existing()
      .each([world = world](const auto&) { RegisterSystems(world); });
}

}  // namespace z13::net
