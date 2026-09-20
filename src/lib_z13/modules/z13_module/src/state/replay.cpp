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

#include <format>
#include <optional>
#include <utility>

#include <boost/container/flat_map.hpp>
#include <flecs.h>

#include <lib_core/components.h>
#include <lib_core/simulation_clock.h>
#include <lib_core/world_serializer.h>
#include <lib_core/world_state.h>

#include <z13/components/gameplay.h>
#include <z13/components/input.h>
#include <z13/components/player_action.h>

namespace z13::state {

namespace {

using ActionValues = boost::container::flat_map<std::pair<uint32_t, z13::input::ActionInfo::IdType>, float>;

// RestoreWorld doesn't touch ActionListener (not state), so without this an action
// touched live after the snapshot keeps a stale prev_value into replay -- an
// edge-triggered action (build, destroy, toggle) that should fire once then silently
// doesn't, since it looks already-on from a tick ago.
void ResetActionValues(flecs::world& world) {
  world.query_builder<z13::input::ActionListener>().build().each(
      [](z13::input::ActionListener& action_listener) {
        for (auto& [action_id, holder] : action_listener.action_values) {
          holder = z13::input::ActionValueHolder{};
        }
      });
}

// Writes `values` into every player entity's ActionListener.action_values in `world`.
// Used both for the pre-replay seed below and by the per-tick injector system.
void ApplyCurrentValues(flecs::world& world, const ActionValues& values) {
  world.query_builder<const z13::gameplay::Player, z13::input::ActionListener>().build().each(
      [&values](const z13::gameplay::Player& player, z13::input::ActionListener& action_listener) {
        for (const auto& [key, value] : values) {
          const auto& [player_id, action_id] = key;
          if (player_id == player.id) {
            action_listener.action_values[action_id].current_value = value;
          }
        }
      });
}

void InjectRecordedActionValues(
    const z13::gameplay::Player& player, z13::input::ActionListener& action_listener,
    const z13::gameplay::ReplayActionState& state) {
  for (const auto& [key, value] : state.current_values) {
    const auto& [player_id, action_id] = key;
    if (player_id == player.id) {
      action_listener.action_values[action_id].current_value = value;
    }
  }
}

void RegisterComponents(flecs::world world) {
  z13::flecs_tools::RegisterComponent<z13::gameplay::ReplayInProgress>(world);
  z13::flecs_tools::RegisterComponent<z13::gameplay::ReplayActionState>(world);
}

void RegisterSystems(flecs::world world) {
  // Set here, not next to `.add(flecs::Singleton)` (see PhysicsSystem::RegisterSystems).
  world.set<z13::gameplay::ReplayActionState>({});

  world.system<
      const z13::gameplay::Player, z13::input::ActionListener, const z13::gameplay::ReplayActionState>(
      "Replay::InjectRecordedActionValues")
      .kind<z13::input::CalculateActionFramePhase>()
      .with<z13::gameplay::ReplayInProgress>()
      .each(InjectRecordedActionValues);
}

}  // namespace

void Replay::Register(flecs::world& world) {
  world.observer<RegisterComponentsEvent>("Replay::RegisterComponents")
      .event(flecs::OnAdd)
      .yield_existing()
      .each([world = world](const auto&) { RegisterComponents(world); });

  world.observer<InitSystemsEvent>("Replay::RegisterSystems")
      .event(flecs::OnAdd)
      .yield_existing()
      .each([world = world](const auto&) { RegisterSystems(world); });
}

std::expected<void, std::string> Replay::Run(
    flecs::world& world, const z13::flecs_tools::TimestampedSnapshot& snapshot, uint64_t target_tick) {
  if (target_tick < snapshot.tick) {
    return std::unexpected("Replay::Run: target_tick precedes the snapshot's tick");
  }
  const std::optional<uint64_t> ticks_per_second = z13::flecs_tools::TicksPerSecond(world);
  if (!ticks_per_second || *ticks_per_second == 0) {
    return std::unexpected("Replay::Run: world has no known tick rate");
  }
  // Should never trigger in practice (same retention window as WorldSnapshotHistory,
  // see player_action_recorder.cpp) -- fails loudly instead of silently reconstructing
  // incomplete state if that invariant is ever broken.
  const uint64_t retained_since_tick = world.get<z13::gameplay::PlayerActionLog>().retained_since_tick;
  if (snapshot.tick < retained_since_tick) {
    return std::unexpected(std::format(
        "Replay::Run: snapshot tick {} is older than the action log's retention window (retained since tick {})",
        snapshot.tick, retained_since_tick));
  }

  if (const auto restored = z13::flecs_tools::RestoreWorld(world, snapshot.snapshot); !restored) {
    return std::unexpected(restored.error());
  }

  // Seed with every record up to the snapshot tick -- the recorder's periodic
  // re-assertion guarantees a held action has an entry no later than this.
  auto& replay_state = world.ensure<z13::gameplay::ReplayActionState>();
  replay_state.current_values.clear();
  const auto& records = world.get<z13::gameplay::PlayerActionLog>().log.Entries();
  auto record_it = records.begin();
  for (; record_it != records.end() && record_it->tick <= snapshot.tick; ++record_it) {
    replay_state.current_values[{record_it->player_id, record_it->action_id}] = record_it->value;
  }

  // Applied directly, before the injector system starts below, so the first tick's
  // ClearActionFramePhase shifts the right value into prev_value.
  ResetActionValues(world);
  ApplyCurrentValues(world, replay_state.current_values);

  world.add<z13::gameplay::ReplayInProgress>();
  const float delta_time = 1.f / static_cast<float>(*ticks_per_second);
  for (uint64_t tick = snapshot.tick + 1; tick <= target_tick; ++tick) {
    for (; record_it != records.end() && record_it->tick == tick; ++record_it) {
      replay_state.current_values[{record_it->player_id, record_it->action_id}] = record_it->value;
    }
    world.progress(delta_time);
  }
  world.remove<z13::gameplay::ReplayInProgress>();

  return {};
}

}  // namespace z13::state
