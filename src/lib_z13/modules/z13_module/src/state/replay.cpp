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

#include <z13_module/state/replay.h>

#include <algorithm>
#include <cstdint>
#include <deque>
#include <utility>

#include <boost/container/flat_map.hpp>
#include <flecs.h>

#include <lib_core/state/rollback.h>
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

namespace ft = z13::flecs_tools;

using ActionValues = boost::container::flat_map<std::pair<uint32_t, z13::input::ActionInfo::IdType>, float>;

bool IsLogDriven(flecs::world world, flecs::entity player) {
  return !player.has<z13::input::CurrentActionListenerTag>() || world.has<z13::net::ClientRole>() ||
      world.has<z13::net::ServerRole>() || world.has<ft::ReplayInProgress>();
}

// ActionListener isn't state, so the restore leaves whatever live play last wrote.
void ResetActionValues(flecs::world& world) {
  world.query_builder<z13::input::ActionListener>().build().each(
      [world](flecs::entity e, z13::input::ActionListener& action_listener) {
        if (!IsLogDriven(world, e)) {
          return;
        }
        for (auto& [action_id, holder] : action_listener.action_values) {
          holder = z13::input::ActionValueHolder{};
        }
      });
}

// Sets prev_value too: the first replayed frame can miss ClearActionFramePhase.
void SeedActionValues(flecs::world& world, const ActionValues& values) {
  world.query_builder<const z13::gameplay::Player, z13::input::ActionListener>().build().each(
      [&values, world](
          flecs::entity e, const z13::gameplay::Player& player, z13::input::ActionListener& action_listener) {
        if (!IsLogDriven(world, e)) {
          return;
        }
        for (const auto& [key, value] : values) {
          const auto& [player_id, action_id] = key;
          if (player_id == player.id) {
            auto& holder = action_listener.action_values[action_id];
            holder.current_value = value;
            holder.prev_value = value;
          }
        }
      });
}

// The log is kept sorted by tick, so each tick's records are one contiguous run.
auto FirstRecordAtTick(const std::deque<z13::gameplay::PlayerActionRecord>& records, uint64_t tick) {
  return std::lower_bound(
      records.begin(), records.end(), tick,
      [](const z13::gameplay::PlayerActionRecord& record, uint64_t value) { return record.tick < value; });
}

void AdvanceRecordedValues(
    flecs::iter& it, size_t, const ft::SimulationClock& clock, const z13::gameplay::PlayerActionLog& log,
    z13::gameplay::RemoteActionState& state) {
  const auto& records = log.log.Entries();
  const auto tick_begin = FirstRecordAtTick(records, clock.tick);

  const bool restarted = !state.synced_tick || *state.synced_tick + 1 != clock.tick;
  if (restarted) {
    state.current_values.clear();
    for (auto record = records.begin(); record != tick_begin; ++record) {
      state.current_values[{record->player_id, record->action_id}] = record->value;
    }
    flecs::world world = it.world();
    ResetActionValues(world);
    SeedActionValues(world, state.current_values);
  }

  for (auto record = tick_begin; record != records.end() && record->tick == clock.tick; ++record) {
    state.current_values[{record->player_id, record->action_id}] = record->value;
  }
  state.synced_tick = clock.tick;
}

bool IsUnconfirmed(
    uint32_t player_id, uint64_t tick, const z13::gameplay::LocalPlayer& local_player,
    const z13::gameplay::ConfirmedInputTicks& confirmed) {
  if (local_player.id == player_id) {
    return false;
  }
  const auto found = confirmed.by_player.find(player_id);
  return found != confirmed.by_player.end() && tick > found->second;
}

// Unknown actions are zeroed too, or a fresh local value would leak through as prediction.
void InjectRecordedActionValues(
    flecs::iter& it, size_t i, const z13::gameplay::Player& player,
    z13::input::ActionListener& action_listener, const z13::gameplay::RemoteActionState& state,
    const z13::input::ActionMap& action_map, const ft::SimulationClock& clock,
    const z13::gameplay::LocalPlayer& local_player, const z13::gameplay::ConfirmedInputTicks& confirmed) {
  if (!IsLogDriven(it.world(), it.entity(i))) {
    return;
  }

  const bool unconfirmed = IsUnconfirmed(player.id, clock.tick, local_player, confirmed);
  for (const auto& action_info : action_map.action_map.get<z13::input::ActionMap::IdTag>()) {
    const auto found = state.current_values.find({player.id, action_info.id});
    const bool released = found == state.current_values.end() || (unconfirmed && !action_info.absolute);
    action_listener.action_values[action_info.id].current_value = released ? 0.f : found->second;
  }
}

void RegisterComponents(flecs::world world) {
  z13::flecs_tools::RegisterComponents<z13::gameplay::RemoteActionState, z13::gameplay::ConfirmedInputTicks>(world);
}

void RegisterSystems(flecs::world world) {
  // Set here, not next to `.add(flecs::Singleton)` (see PhysicsSystem::RegisterSystems).
  world.set<z13::gameplay::RemoteActionState>({});
  world.set<z13::gameplay::ConfirmedInputTicks>({});

  world.system<
      const ft::SimulationClock, const z13::gameplay::PlayerActionLog, z13::gameplay::RemoteActionState>(
      "Replay::AdvanceRecordedValues")
      .kind<z13::input::RemoteActionFramePhase>()
      .write<z13::input::ActionListener>()
      .each(AdvanceRecordedValues);

  world.system<
      const z13::gameplay::Player, z13::input::ActionListener, const z13::gameplay::RemoteActionState,
      const z13::input::ActionMap, const ft::SimulationClock, const z13::gameplay::LocalPlayer,
      const z13::gameplay::ConfirmedInputTicks>(
      "Replay::InjectRecordedActionValues")
      .kind<z13::input::RemoteActionFramePhase>()
      .each(InjectRecordedActionValues);
}

}  // namespace

void Replay::Register(flecs::world& world) {
  OnRegisterComponents(world, RegisterComponents);

  OnInitSystems(world, RegisterSystems);
}

}  // namespace z13::state
